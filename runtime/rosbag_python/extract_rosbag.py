#!/usr/bin/env python3
"""Stream numeric and string fields from ROS1/ROS2 bags to RosSignalStudio."""

from __future__ import annotations

import argparse
import base64
import dataclasses
import json
import math
import struct
import sys
import time
from collections import OrderedDict
from collections.abc import Iterator
from functools import lru_cache
from pathlib import Path
from typing import BinaryIO, TextIO
from urllib.parse import quote

import numpy as np
from rosbags.highlevel import AnyReader
from rosbags.rosbag1 import Reader as Ros1Reader
from rosbags.typesys import Stores, get_typestore


def array_limit(value: str) -> int:
    parsed = int(value)
    if not 0 <= parsed <= 100_000:
        raise argparse.ArgumentTypeError("must be between 0 and 100000")
    return parsed


@lru_cache(maxsize=131072)
def field_path(prefix: str, field: str) -> str:
    return f"{prefix}/{field}"


@lru_cache(maxsize=131072)
def array_path(prefix: str, index: int) -> str:
    return f"{prefix}[{index}]"


@lru_cache(maxsize=4096)
def public_dataclass_fields(value_type: type[object]) -> tuple[dataclasses.Field[object], ...]:
    return tuple(field for field in dataclasses.fields(value_type) if not field.name.startswith("_"))


def encode_name(name: str) -> str:
    return quote(name, safe="/[]_.:-")


def flatten(value: object, prefix: str, max_array: int) -> Iterator[tuple[str, str, object]]:
    if isinstance(value, (bool, int, float, np.number)):
        number = float(value)
        if math.isfinite(number):
            yield "N", prefix, number
        return

    if isinstance(value, str):
        yield "S", prefix, value
        return

    if dataclasses.is_dataclass(value):
        for field in public_dataclass_fields(type(value)):
            child = getattr(value, field.name)
            yield from flatten(child, field_path(prefix, field.name), max_array)
        return

    if isinstance(value, np.ndarray):
        if value.ndim == 0:
            yield from flatten(value.item(), prefix, max_array)
            return
        for index, child in enumerate(value.flat[:max_array]):
            yield from flatten(child, array_path(prefix, index), max_array)
        return

    if isinstance(value, (list, tuple)):
        for index, child in enumerate(value[:max_array]):
            yield from flatten(child, array_path(prefix, index), max_array)


def resolve_input(path: Path) -> Path:
    path = path.resolve()
    if path.suffix.lower() in {".db3", ".mcap"}:
        metadata = path.parent / "metadata.yaml"
        return path.parent if metadata.exists() else path
    return path


def is_ros1_bag(path: Path) -> bool:
    return path.is_file() and path.suffix.lower() == ".bag"


def ros1_typename(msgtype: str) -> str:
    parts = msgtype.split("/")
    if len(parts) >= 3 and parts[-2] == "msg":
        return "/".join((*parts[:-2], parts[-1]))
    return msgtype


class StageMetrics:
    """Collect phase timings without timing every message."""

    SAMPLE_INTERVAL = 128

    def __init__(self) -> None:
        self.open_seconds = 0.0
        self.first_message_seconds = 0.0
        self.deserialize_seconds = 0.0
        self.flatten_seconds = 0.0
        self.write_seconds = 0.0
        self._deserialize_samples = 0
        self._flatten_samples = 0

    def sample(self, message_number: int) -> bool:
        return message_number <= 16 or message_number % self.SAMPLE_INTERVAL == 0

    def add_deserialize_sample(self, seconds: float) -> None:
        self.deserialize_seconds += seconds
        self._deserialize_samples += 1

    def add_flatten_sample(self, seconds: float) -> None:
        self.flatten_seconds += seconds
        self._flatten_samples += 1

    def scaled_deserialize_seconds(self, messages: int) -> float:
        if not self._deserialize_samples:
            return 0.0
        return self.deserialize_seconds * messages / self._deserialize_samples

    def scaled_flatten_seconds(self, messages: int) -> float:
        if not self._flatten_samples:
            return 0.0
        return self.flatten_seconds * messages / self._flatten_samples


class TextWriter:
    MAX_STRING_SIZE = 8 * 1024 * 1024

    def __init__(self, stream: TextIO, metrics: StageMetrics) -> None:
        self.stream = stream
        self.metrics = metrics
        self.output: list[str] = []

    def emit(self, kind: str, timestamp: int, name: str, value: object) -> None:
        if kind == "N":
            self.output.append(f"N\t{timestamp}\t{encode_name(name)}\t{float(value):.17g}\n")
        else:
            raw = str(value).encode("utf-8")
            if len(raw) > self.MAX_STRING_SIZE:
                raise ValueError("string field exceeds legacy protocol record limit")
            encoded = base64.b64encode(raw).decode("ascii")
            self.output.append(f"S\t{timestamp}\t{encode_name(name)}\t{encoded}\n")
        if len(self.output) >= 4096:
            self.flush()

    def flush(self) -> None:
        if not self.output:
            return
        started = time.perf_counter()
        self.stream.write("".join(self.output))
        self.stream.flush()
        self.output.clear()
        self.metrics.write_seconds += time.perf_counter() - started

    def done(self, messages: int, emitted: int) -> None:
        del messages, emitted
        self.flush()


class BinaryWriter:
    MAGIC = b"RSPJBAG\0"
    MAX_FRAME_SIZE = 64 * 1024 * 1024
    MAX_NAME_SIZE = 1024 * 1024
    STRING_BLOCK_LIMIT = 4 * 1024 * 1024
    SERIES_LIMIT = 2048
    TOTAL_LIMIT = 8192

    def __init__(self, stream: BinaryIO, metrics: StageMetrics) -> None:
        self.stream = stream
        self.metrics = metrics
        self.series: dict[tuple[int, str], int] = {}
        self.buffers: OrderedDict[int, tuple[int, list[int], list[object]]] = OrderedDict()
        self.buffer_bytes: dict[int, int] = {}
        self.next_id = 1
        self.buffered_points = 0
        self._write(self.MAGIC + struct.pack("<HH", 1, 0))

    def _write(self, data: bytes) -> None:
        started = time.perf_counter()
        self.stream.write(data)
        self.metrics.write_seconds += time.perf_counter() - started

    def _frame(self, frame_type: int, payload: bytes) -> None:
        if len(payload) > self.MAX_FRAME_SIZE:
            raise ValueError("binary protocol frame exceeds 64 MiB")
        self._write(struct.pack("<BI", frame_type, len(payload)) + payload)

    def emit(self, kind: str, timestamp: int, name: str, value: object) -> None:
        kind_id = 1 if kind == "N" else 2
        encoded_value: object = value
        if kind_id == 2:
            encoded_value = str(value).encode("utf-8")
            if len(encoded_value) + 20 > self.STRING_BLOCK_LIMIT:
                raise ValueError("string field exceeds binary protocol block limit")
        key = (kind_id, name)
        series_id = self.series.get(key)
        if series_id is None:
            encoded_name = name.encode("utf-8")
            if not encoded_name or len(encoded_name) > self.MAX_NAME_SIZE:
                raise ValueError("series name exceeds binary protocol limit")
            series_id = self.next_id
            self.next_id += 1
            self.series[key] = series_id
            payload = struct.pack("<IBI", series_id, kind_id, len(encoded_name)) + encoded_name
            self._frame(1, payload)

        buffer = self.buffers.get(series_id)
        if buffer is None:
            buffer = (kind_id, [], [])
            self.buffers[series_id] = buffer
            self.buffer_bytes[series_id] = 8
        record_bytes = 16 if kind_id == 1 else 12 + len(encoded_value)
        if buffer[1] and self.buffer_bytes[series_id] + record_bytes > self.STRING_BLOCK_LIMIT:
            self._flush_series(series_id)
            buffer = (kind_id, [], [])
            self.buffers[series_id] = buffer
            self.buffer_bytes[series_id] = 8
        buffer[1].append(timestamp)
        buffer[2].append(encoded_value)
        self.buffer_bytes[series_id] += record_bytes
        self.buffered_points += 1

        if len(buffer[1]) >= self.SERIES_LIMIT:
            self._flush_series(series_id)
        elif self.buffered_points >= self.TOTAL_LIMIT:
            self.flush()

    def _flush_series(self, series_id: int) -> None:
        kind_id, timestamps, values = self.buffers.pop(series_id)
        self.buffer_bytes.pop(series_id)
        count = len(timestamps)
        self.buffered_points -= count
        if kind_id == 1:
            payload = (
                struct.pack("<II", series_id, count)
                + struct.pack(f"<{count}q", *timestamps)
                + struct.pack(f"<{count}d", *(float(value) for value in values))
            )
            self._frame(2, payload)
            return

        chunks = [struct.pack("<II", series_id, count)]
        for timestamp, value in zip(timestamps, values):
            encoded = bytes(value)
            chunks.append(struct.pack("<qI", timestamp, len(encoded)))
            chunks.append(encoded)
        self._frame(3, b"".join(chunks))

    def flush(self) -> None:
        for series_id in list(self.buffers):
            self._flush_series(series_id)
        started = time.perf_counter()
        self.stream.flush()
        self.metrics.write_seconds += time.perf_counter() - started

    def done(self, messages: int, emitted: int) -> None:
        self.flush()
        self._frame(4, struct.pack("<QQ", messages, emitted))
        started = time.perf_counter()
        self.stream.flush()
        self.metrics.write_seconds += time.perf_counter() - started


class RawWriter:
    MAGIC = b"RSPJRAW\0"
    MAX_FRAME_SIZE = 64 * 1024 * 1024
    BATCH_LIMIT = 4 * 1024 * 1024

    def __init__(self, stream: BinaryIO, metrics: StageMetrics) -> None:
        self.stream = stream
        self.metrics = metrics
        self._chunks: list[bytes] = []
        self._chunk_bytes = 4
        self._chunk_count = 0
        self.payload_bytes = 0
        self._write(self.MAGIC + struct.pack("<HH", 1, 0))

    def _write(self, data: bytes) -> None:
        started = time.perf_counter()
        self.stream.write(data)
        self.metrics.write_seconds += time.perf_counter() - started

    def _frame(self, frame_type: int, payload: bytes) -> None:
        if len(payload) > self.MAX_FRAME_SIZE:
            raise ValueError("raw protocol frame exceeds 64 MiB")
        self._write(struct.pack("<BI", frame_type, len(payload)) + payload)

    def emit_connection(self, connection_id: int, topic: str, msgtype: str, schema: str) -> None:
        topic_b = topic.encode("utf-8")
        type_b = msgtype.encode("utf-8")
        schema_b = schema.encode("utf-8")
        payload = (
            struct.pack("<I", connection_id)
            + struct.pack("<I", len(topic_b))
            + topic_b
            + struct.pack("<I", len(type_b))
            + type_b
            + struct.pack("<I", len(schema_b))
            + schema_b
        )
        self._frame(1, payload)

    def emit_message(self, connection_id: int, timestamp: int, rawdata: bytes) -> None:
        record = struct.pack("<IqI", connection_id, timestamp, len(rawdata)) + rawdata
        if self._chunks and self._chunk_bytes + len(record) > self.BATCH_LIMIT:
            self._flush_messages()
        self._chunks.append(record)
        self._chunk_bytes += len(record)
        self._chunk_count += 1
        self.payload_bytes += len(rawdata)
        if self._chunk_bytes >= self.BATCH_LIMIT:
            self._flush_messages()

    def _flush_messages(self) -> None:
        if not self._chunks:
            return
        payload = struct.pack("<I", self._chunk_count) + b"".join(self._chunks)
        self._chunks.clear()
        self._chunk_bytes = 4
        self._chunk_count = 0
        self._frame(2, payload)

    def flush(self) -> None:
        self._flush_messages()
        started = time.perf_counter()
        self.stream.flush()
        self.metrics.write_seconds += time.perf_counter() - started

    def done(self, messages: int, emitted: int) -> None:
        del emitted
        self.flush()
        self._frame(3, struct.pack("<QQ", messages, self.payload_bytes))
        started = time.perf_counter()
        self.stream.flush()
        self.metrics.write_seconds += time.perf_counter() - started


def load_topics_file(path: Path) -> set[str]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, list) or any(not isinstance(topic, str) for topic in value):
        raise ValueError("--topics-file must contain a JSON array of strings")
    return set(value)


def inspect_connections(connections: object) -> dict[str, object]:
    topics: dict[tuple[str, str], int] = {}
    schemas: dict[tuple[str, str], str] = {}
    for connection in connections:  # type: ignore[attr-defined]
        key = (connection.topic, connection.msgtype)
        topics[key] = topics.get(key, 0) + connection.msgcount
        if key not in schemas:
            schemas[key] = connection.msgdef.data if connection.msgdef else ""
    return {
        "version": 1,
        "totalMessages": sum(topics.values()),
        "topics": [
            {
                "name": name,
                "type": msgtype,
                "messageCount": count,
                "schema": schemas[(name, msgtype)],
            }
            for (name, msgtype), count in sorted(topics.items())
        ],
    }


def print_metrics(metrics: StageMetrics, messages: int, total_seconds: float) -> None:
    values = (
        ("open_seconds", metrics.open_seconds),
        ("first_message_seconds", metrics.first_message_seconds),
        ("deserialize_seconds", metrics.scaled_deserialize_seconds(messages)),
        ("flatten_seconds", metrics.scaled_flatten_seconds(messages)),
        ("write_seconds", metrics.write_seconds),
        ("total_seconds", total_seconds),
    )
    for name, value in values:
        print(f"METRIC\t{name}\t{value:.9f}", file=sys.stderr, flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bag")
    parser.add_argument("--max-array", type=array_limit, default=100)
    parser.add_argument("--protocol", choices=("text", "binary-v1", "raw-v1"), default="text")
    parser.add_argument("--inspect", action="store_true")
    parser.add_argument("--topics-file", type=Path)
    args = parser.parse_args()

    bag = resolve_input(Path(args.bag))
    use_raw = args.protocol == "raw-v1"
    metrics = StageMetrics()
    emitted = 0
    messages = 0
    warned_errors: set[tuple[str, str, type[BaseException]]] = set()
    started_total = time.perf_counter()

    def warn_once(topic: str, stage: str, error: BaseException) -> None:
        key = (topic, stage, type(error))
        if key in warned_errors:
            return
        warned_errors.add(key)
        print(
            f"WARN\t{encode_name(topic)}\t{stage}: {type(error).__name__}: {error}",
            file=sys.stderr,
            flush=True,
        )

    try:
        if use_raw and not is_ros1_bag(bag):
            raise ValueError("raw-v1 is only supported for ROS1 .bag files")
        requested_topics = load_topics_file(args.topics_file) if args.topics_file else None
        started_open = time.perf_counter()
        if use_raw or (args.inspect and is_ros1_bag(bag)):
            reader_cm: object = Ros1Reader(bag)
        else:
            typestore = get_typestore(Stores.LATEST)
            reader_cm = AnyReader([bag], default_typestore=typestore)
        with reader_cm as reader:
            metrics.open_seconds = time.perf_counter() - started_open
            if args.inspect:
                print(
                    json.dumps(
                        inspect_connections(reader.connections),
                        ensure_ascii=False,
                        separators=(",", ":"),
                    )
                )
                print_metrics(metrics, 0, time.perf_counter() - started_total)
                return 0

            selected_connections = [
                connection
                for connection in reader.connections
                if requested_topics is None or connection.topic in requested_topics
            ]
            total = sum(connection.msgcount for connection in selected_connections)
            control_stream = sys.stderr if args.protocol != "text" else sys.stdout
            print(
                f"INFO\t{len(selected_connections)}\t{total}",
                file=control_stream,
                flush=True,
            )

            if use_raw:
                raw_writer = RawWriter(sys.stdout.buffer, metrics)
                for connection in selected_connections:
                    topic = connection.topic.rstrip("/") or "/"
                    schema = connection.msgdef.data if connection.msgdef else ""
                    raw_writer.emit_connection(
                        connection.id, topic, ros1_typename(connection.msgtype), schema
                    )
                waiting_for_first = time.perf_counter()
                message_stream = (
                    reader.messages(connections=selected_connections)
                    if selected_connections
                    else ()
                )
                for connection, timestamp, rawdata in message_stream:
                    messages += 1
                    if messages == 1:
                        metrics.first_message_seconds = (
                            time.perf_counter() - waiting_for_first
                        )
                    raw_writer.emit_message(connection.id, timestamp, rawdata)
                    emitted += 1
                    if messages % 2000 == 0:
                        print(
                            f"PROGRESS\t{messages}\t{total}\t{emitted}",
                            file=sys.stderr,
                            flush=True,
                        )
                raw_writer.done(messages, emitted)
            else:
                writer: TextWriter | BinaryWriter = (
                    BinaryWriter(sys.stdout.buffer, metrics)
                    if args.protocol == "binary-v1"
                    else TextWriter(sys.stdout, metrics)
                )
                waiting_for_first = time.perf_counter()
                message_stream = (
                    reader.messages(connections=selected_connections)
                    if selected_connections
                    else ()
                )
                for connection, timestamp, rawdata in message_stream:
                    messages += 1
                    if messages == 1:
                        metrics.first_message_seconds = (
                            time.perf_counter() - waiting_for_first
                        )
                    sampled = metrics.sample(messages)
                    try:
                        stage_started = time.perf_counter() if sampled else 0.0
                        message = reader.deserialize(rawdata, connection.msgtype)
                        if sampled:
                            metrics.add_deserialize_sample(
                                time.perf_counter() - stage_started
                            )
                    except Exception as error:
                        warn_once(connection.topic, "deserialize", error)
                        continue

                    topic = connection.topic.rstrip("/") or "/"
                    try:
                        stage_started = time.perf_counter() if sampled else 0.0
                        flattened = flatten(message, topic, args.max_array)
                        for kind, name, value in flattened:
                            writer.emit(kind, timestamp, name, value)
                            emitted += 1
                        if sampled:
                            metrics.add_flatten_sample(
                                time.perf_counter() - stage_started
                            )
                    except Exception as error:
                        warn_once(connection.topic, "flatten", error)
                        continue

                    if messages % 500 == 0:
                        print(
                            f"PROGRESS\t{messages}\t{total}\t{emitted}",
                            file=sys.stderr,
                            flush=True,
                        )
                writer.done(messages, emitted)

        print(f"DONE\t{messages}\t{emitted}", file=sys.stderr, flush=True)
        print_metrics(metrics, messages, time.perf_counter() - started_total)
        return 0
    except Exception as error:
        print(f"ERROR\t{type(error).__name__}: {error}", file=sys.stderr, flush=True)
        print_metrics(metrics, messages, time.perf_counter() - started_total)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
