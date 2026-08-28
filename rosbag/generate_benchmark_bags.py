"""Generate deterministic ROS1, ROS2 SQLite, or ROS2 MCAP benchmark bags."""

from __future__ import annotations

import argparse
import shutil
from math import sin
from pathlib import Path

import numpy as np
from rosbags.rosbag1 import Writer as Ros1Writer
from rosbags.rosbag2 import StoragePlugin
from rosbags.rosbag2 import Writer as Ros2Writer
from rosbags.typesys import Stores, get_typestore


def nonnegative(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def ratio(value: str) -> float:
    parsed = float(value)
    if not 0.0 <= parsed <= 1.0:
        raise argparse.ArgumentTypeError("must be between 0 and 1")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--format",
        choices=("ros1", "ros2-sqlite", "ros2-mcap"),
        required=True,
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--messages", type=nonnegative, default=10_000)
    parser.add_argument("--topics", type=nonnegative, default=10)
    parser.add_argument("--array-width", type=nonnegative, default=16)
    parser.add_argument("--empty-string-ratio", type=ratio, default=0.1)
    return parser.parse_args()


def numeric_value(topic_index: int, message_index: int, element_index: int) -> float:
    return topic_index * 1000.0 + element_index + sin(message_index / (10 + topic_index))


def prepare_output(path: Path) -> Path:
    output = path.resolve()
    if output.exists():
        if output.is_dir():
            shutil.rmtree(output)
        else:
            output.unlink()
    output.parent.mkdir(parents=True, exist_ok=True)
    return output


def write_bag(args: argparse.Namespace, output: Path) -> None:
    ros1 = args.format == "ros1"
    store = get_typestore(Stores.ROS1_NOETIC if ros1 else Stores.ROS2_JAZZY)
    array_type = store.types["std_msgs/msg/Float64MultiArray"]
    string_type = store.types["std_msgs/msg/String"]
    layout_type = store.types["std_msgs/msg/MultiArrayLayout"]
    dimension_type = store.types["std_msgs/msg/MultiArrayDimension"]

    if ros1:
        writer = Ros1Writer(output)
    else:
        plugin = (
            StoragePlugin.SQLITE3
            if args.format == "ros2-sqlite"
            else StoragePlugin.MCAP
        )
        writer = Ros2Writer(output, version=9, storage_plugin=plugin)

    with writer:
        numeric_connections = [
            writer.add_connection(
                f"/benchmark/numeric_{index:03d}",
                "std_msgs/msg/Float64MultiArray",
                typestore=store,
            )
            for index in range(args.topics)
        ]
        text_connection = writer.add_connection(
            "/benchmark/text", "std_msgs/msg/String", typestore=store
        )
        layout = layout_type(
            dim=[
                dimension_type(
                    label="samples", size=args.array_width, stride=args.array_width
                )
            ],
            data_offset=0,
        )
        empty_period = (
            max(1, round(1.0 / args.empty_string_ratio))
            if args.empty_string_ratio
            else 0
        )
        serialize = store.serialize_ros1 if ros1 else store.serialize_cdr
        start_ns = 1_700_000_000_000_000_000

        for message_index in range(args.messages):
            timestamp = start_ns + message_index * 1_000_000
            for topic_index, connection in enumerate(numeric_connections):
                values = np.fromiter(
                    (
                        numeric_value(topic_index, message_index, element_index)
                        for element_index in range(args.array_width)
                    ),
                    dtype=np.float64,
                    count=args.array_width,
                )
                message = array_type(layout=layout, data=values)
                writer.write(
                    connection,
                    timestamp,
                    serialize(message, connection.msgtype),
                )

            text = (
                ""
                if empty_period and message_index % empty_period == 0
                else f"state-{message_index % 17}"
            )
            writer.write(
                text_connection,
                timestamp,
                serialize(string_type(data=text), text_connection.msgtype),
            )


def main() -> None:
    args = parse_args()
    output = prepare_output(args.output)
    write_bag(args, output)
    print(output)


if __name__ == "__main__":
    main()
