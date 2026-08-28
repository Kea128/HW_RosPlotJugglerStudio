"""Generate a deterministic ROS1 numeric bag for testing and benchmarks."""

import argparse
from math import sin
from pathlib import Path

import numpy as np
from rosbags.rosbag1 import Writer
from rosbags.typesys import Stores, get_typestore



def nonnegative(value: str) -> int:
    """Parse a non-negative integer argument."""
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def parse_args() -> argparse.Namespace:
    """Parse command-line options while preserving the original defaults."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).parent / "ros1" / "numeric_signals.bag",
    )
    parser.add_argument("--messages", type=nonnegative, default=500)
    parser.add_argument("--numeric-topics", type=nonnegative, default=2)
    parser.add_argument(
        "--string-every",
        type=nonnegative,
        default=100,
        help="emit a string every N timestamps; zero disables strings",
    )
    parser.add_argument(
        "--array-width",
        type=nonnegative,
        default=0,
        help="also emit one Float64MultiArray of this width; zero disables arrays",
    )
    return parser.parse_args()


def numeric_topic(index: int) -> str:
    """Return stable topic names, including the two historical defaults."""
    if index == 0:
        return "/vehicle/speed"
    if index == 1:
        return "/vehicle/steering_angle"
    return f"/vehicle/numeric_{index + 1:03d}"


def numeric_value(topic_index: int, message_index: int) -> float:
    """Return a deterministic, topic-specific numeric signal."""
    if topic_index == 0:
        return 12.0 + 2.0 * sin(message_index / 30)
    if topic_index == 1:
        return 0.25 * sin(message_index / 15)
    return topic_index + sin(message_index / (10 + topic_index))


def main() -> None:
    """Write the requested deterministic ROS1 bag."""
    args = parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)

    store = get_typestore(Stores.ROS1_NOETIC)
    float64 = store.types["std_msgs/msg/Float64"]
    string = store.types["std_msgs/msg/String"]
    float64_array = store.types["std_msgs/msg/Float64MultiArray"]
    array_layout = store.types["std_msgs/msg/MultiArrayLayout"]
    array_dimension = store.types["std_msgs/msg/MultiArrayDimension"]

    with Writer(output) as writer:
        numeric_connections = [
            writer.add_connection(
                numeric_topic(index), "std_msgs/msg/Float64", typestore=store
            )
            for index in range(args.numeric_topics)
        ]
        mode = (
            writer.add_connection(
                "/vehicle/mode", "std_msgs/msg/String", typestore=store
            )
            if args.string_every
            else None
        )
        array_connection = (
            writer.add_connection(
                "/vehicle/numeric_array",
                "std_msgs/msg/Float64MultiArray",
                typestore=store,
            )
            if args.array_width
            else None
        )
        layout = (
            array_layout(
                dim=[
                    array_dimension(
                        label="samples",
                        size=args.array_width,
                        stride=args.array_width,
                    )
                ],
                data_offset=0,
            )
            if args.array_width
            else None
        )

        start_ns = 1_700_000_000_000_000_000
        for index in range(args.messages):
            timestamp = start_ns + index * 20_000_000
            for topic_index, connection in enumerate(numeric_connections):
                value = numeric_value(topic_index, index)
                writer.write(
                    connection,
                    timestamp,
                    store.serialize_ros1(float64(data=value), connection.msgtype),
                )
            if mode is not None and index % args.string_every == 0:
                writer.write(
                    mode,
                    timestamp,
                    store.serialize_ros1(
                        string(data=f"mode-{index // args.string_every}"),
                        mode.msgtype,
                    ),
                )
            if array_connection is not None:
                values = np.fromiter(
                    (
                        numeric_value(array_index + 2, index)
                        for array_index in range(args.array_width)
                    ),
                    dtype=np.float64,
                    count=args.array_width,
                )
                writer.write(
                    array_connection,
                    timestamp,
                    store.serialize_ros1(
                        float64_array(layout=layout, data=values),
                        array_connection.msgtype,
                    ),
                )

    print(output)


if __name__ == "__main__":
    main()
