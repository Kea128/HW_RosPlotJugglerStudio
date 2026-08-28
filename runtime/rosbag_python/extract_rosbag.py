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
from rosbags.typesys import Stores, get_typestore


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
    if path.suffix.lower() == ".db3":
        metadata = path.parent / "metadata.yaml"
        return path.parent if metadata.exists() else path
    return path


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
    def __init__(self, stream: TextIO, metrics: StageMetrics) -> None:
        self.stream = stream
        self.metrics = metrics
        self.output: list[str] = []

    def emit(self, kind: str, timestamp: int, name: str, value: object) -> None:
        if kind == "N":
            self.output.append(f"N\t{timestamp}\t{encode_name(name)}\t{float(value):.17g}\n")
        else:
            encoded = base64.b64encode(str(value).encode("utf-8")).decode("ascii")
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
    SERIES_LIMIT = 2048
    TOTAL_LIMIT = 8192

    def __init__(self, stream: BinaryIO, metrics: StageMetrics) -> None:
        self.stream = stream
        self.metrics = metrics
        self.series: dict[tuple[int, str], int] = {}
        self.buffers: OrderedDict[int, tuple[int, list[int], list[object]]] = OrderedDict()
        self.next_id = 1
        self.buffered_points = 0
        self._write(self.MAGIC + struct.pack("<HH", 1, 0))

    def _write(self, data: bytes) -> None:
        started = time.perf_counter()
        self.stream.write(data)
        self.metrics.write_seconds += time.perf_counter() - started

    def _frame(self, frame_type: int, payload: bytes) -> None:
        self._write(struct.pack("<BI", frame_type, len(payload)) + payload)

    def emit(self, kind: str, timestamp: int, name: str, value: object) -> None:
        kind_id = 1 if kind == "N" else 2
        key = (kind_id, name)
        series_id = self.series.get(key)
        if series_id is None:
            series_id = self.next_id
            self.next_id += 1
            self.series[key] = series_id
            encoded_name = name.encode("utf-8")
            payload = struct.pack("<IBI", series_id, kind_id, len(encoded_name)) + encoded_name
            self._frame(1, payload)

        buffer = self.buffers.get(series_id)
        if buffer is None:
            buffer = (kind_id, [], [])
            self.buffers[series_id] = buffer
        buffer[1].append(timestamp)
        buffer[2].append(value)
        self.buffered_points += 1

        if len(buffer[1]) >= self.SERIES_LIMIT:
            self._flush_series(series_id)
        elif self.buffered_points >= self.TOTAL_LIMIT:
            self.flush()

    def _flush_series(self, series_id: int) -> None:
        kind_id, timestamps, values = self.buffers.pop(series_id)
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
            encoded = str(value).encode("utf-8")
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


def load_topics_file(path: Path) -> set[str]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, list) or any(not isinstance(topic, str) for topic in value):
        raise ValueError("--topics-file must contain a JSON array of strings")
    return set(value)


def inspect_reader(reader: AnyReader) -> dict[str, object]:
    topics: dict[tuple[str, str], int] = {}
    for connection in reader.connections:
        key = (connection.topic, connection.msgtype)
        topics[key] = topics.get(key, 0) + connection.msgcount
    return {
        "version": 1,
        "totalMessages": sum(topics.values()),
        "topics": [
            {"name": name, "type": msgtype, "messageCount": count}
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
    parser.add_argument("--max-array", type=int, default=100)
    parser.add_argument("--protocol", choices=("text", "binary-v1"), default="text")
    parser.add_argument("--inspect", action="store_true")
    parser.add_argument("--topics-file", type=Path)
    args = parser.parse_args()

    bag = resolve_input(Path(args.bag))
    typestore = get_typestore(Stores.LATEST)
    metrics = StageMetrics()
    emitted = 0
    messages = 0
    started_total = time.perf_counter()

    try:
        requested_topics = load_topics_file(args.topics_file) if args.topics_file else None
        started_open = time.perf_counter()
        with AnyReader([bag], default_typestore=typestore) as reader:
            metrics.open_seconds = time.perf_counter() - started_open
            if args.inspect:
                print(json.dumps(inspect_reader(reader), ensure_ascii=False, separators=(",", ":")))
                print_metrics(metrics, 0, time.perf_counter() - started_total)
                return 0

            selected_connections = [
                connection
                for connection in reader.connections
                if requested_topics is None or connection.topic in requested_topics
            ]
            total = sum(connection.msgcount for connection in selected_connections)
            if args.protocol == "binary-v1":
                writer: TextWriter | BinaryWriter = BinaryWriter(sys.stdout.buffer, metrics)
                control_stream = sys.stderr
            else:
                writer = TextWriter(sys.stdout, metrics)
                control_stream = sys.stdout
            print(
                f"INFO\t{len(selected_connections)}\t{total}",
                file=control_stream,
                flush=True,
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
                    metrics.first_message_seconds = time.perf_counter() - waiting_for_first
                sampled = metrics.sample(messages)
                try:
                    stage_started = time.perf_counter() if sampled else 0.0
                    message = reader.deserialize(rawdata, connection.msgtype)
                    if sampled:
                        metrics.add_deserialize_sample(time.perf_counter() - stage_started)
                except Exception as error:  # Continue when one custom type is unavailable.
                    print(
                        f"WARN\t{encode_name(connection.topic)}\t"
                        f"{type(error).__name__}: {error}",
                        file=sys.stderr,
                        flush=True,
                    )
                    continue

                topic = connection.topic.rstrip("/") or "/"
                try:
                    stage_started = time.perf_counter() if sampled else 0.0
                    flattened = flatten(message, topic, args.max_array)
                    for kind, name, value in flattened:
                        writer.emit(kind, timestamp, name, value)
                        emitted += 1
                    if sampled:
                        metrics.add_flatten_sample(time.perf_counter() - stage_started)
                except Exception as error:
                    print(
                        f"WARN\t{encode_name(connection.topic)}\t"
                        f"{type(error).__name__}: {error}",
                        file=sys.stderr,
                        flush=True,
                    )
                    continue

                if messages % 500 == 0:
                    print(f"PROGRESS\t{messages}\t{total}\t{emitted}", file=sys.stderr, flush=True)

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
