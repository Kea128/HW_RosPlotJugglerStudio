#!/usr/bin/env python3
"""Stream numeric and string fields from ROS1/ROS2 bags to RosSignalStudio."""

from __future__ import annotations

import argparse
import base64
import dataclasses
import math
import sys
from collections.abc import Iterator
from pathlib import Path
from urllib.parse import quote

import numpy as np
from rosbags.highlevel import AnyReader
from rosbags.typesys import Stores, get_typestore


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
        for field in dataclasses.fields(value):
            if field.name.startswith("_"):
                continue
            child = getattr(value, field.name)
            yield from flatten(child, f"{prefix}/{field.name}", max_array)
        return

    if isinstance(value, np.ndarray):
        if value.ndim == 0:
            yield from flatten(value.item(), prefix, max_array)
            return
        for index, child in enumerate(value.flat[:max_array]):
            yield from flatten(child, f"{prefix}[{index}]", max_array)
        return

    if isinstance(value, (list, tuple)):
        for index, child in enumerate(value[:max_array]):
            yield from flatten(child, f"{prefix}[{index}]", max_array)


def resolve_input(path: Path) -> Path:
    path = path.resolve()
    if path.suffix.lower() == ".db3":
        metadata = path.parent / "metadata.yaml"
        return path.parent if metadata.exists() else path
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bag")
    parser.add_argument("--max-array", type=int, default=100)
    args = parser.parse_args()

    bag = resolve_input(Path(args.bag))
    typestore = get_typestore(Stores.LATEST)
    emitted = 0
    messages = 0
    output: list[str] = []

    try:
        with AnyReader([bag], default_typestore=typestore) as reader:
            total = sum(connection.msgcount for connection in reader.connections)
            print(f"INFO\t{len(reader.connections)}\t{total}", flush=True)

            for connection, timestamp, rawdata in reader.messages():
                messages += 1
                try:
                    message = reader.deserialize(rawdata, connection.msgtype)
                except Exception as error:  # Continue when one custom type is unavailable.
                    print(
                        f"WARN\t{encode_name(connection.topic)}\t"
                        f"{type(error).__name__}: {error}",
                        file=sys.stderr,
                        flush=True,
                    )
                    continue

                topic = connection.topic.rstrip("/") or "/"
                for kind, name, value in flatten(message, topic, args.max_array):
                    if kind == "N":
                        output.append(f"N\t{timestamp}\t{encode_name(name)}\t{value:.17g}\n")
                    else:
                        encoded = base64.b64encode(str(value).encode("utf-8")).decode("ascii")
                        output.append(f"S\t{timestamp}\t{encode_name(name)}\t{encoded}\n")
                    emitted += 1

                if len(output) >= 4096:
                    sys.stdout.write("".join(output))
                    sys.stdout.flush()
                    output.clear()

                if messages % 500 == 0:
                    print(f"PROGRESS\t{messages}\t{total}\t{emitted}", file=sys.stderr, flush=True)

        if output:
            sys.stdout.write("".join(output))
            sys.stdout.flush()
        print(f"DONE\t{messages}\t{emitted}", file=sys.stderr, flush=True)
        return 0
    except Exception as error:
        print(f"ERROR\t{type(error).__name__}: {error}", file=sys.stderr, flush=True)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
