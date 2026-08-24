"""Generate a tiny deterministic ROS1 numeric bag for regression testing."""

from math import sin
from pathlib import Path

from rosbags.rosbag1 import Writer
from rosbags.typesys import Stores, get_typestore

output = Path(__file__).parent / "ros1" / "numeric_signals.bag"
output.parent.mkdir(parents=True, exist_ok=True)
output.unlink(missing_ok=True)

store = get_typestore(Stores.ROS1_NOETIC)
float64 = store.types["std_msgs/msg/Float64"]
string = store.types["std_msgs/msg/String"]

with Writer(output) as writer:
    speed = writer.add_connection(
        "/vehicle/speed", "std_msgs/msg/Float64", typestore=store
    )
    steering = writer.add_connection(
        "/vehicle/steering_angle", "std_msgs/msg/Float64", typestore=store
    )
    mode = writer.add_connection(
        "/vehicle/mode", "std_msgs/msg/String", typestore=store
    )
    start_ns = 1_700_000_000_000_000_000
    for index in range(500):
        timestamp = start_ns + index * 20_000_000
        writer.write(
            speed,
            timestamp,
            store.serialize_ros1(float64(data=12.0 + 2.0 * sin(index / 30)), speed.msgtype),
        )
        writer.write(
            steering,
            timestamp,
            store.serialize_ros1(float64(data=0.25 * sin(index / 15)), steering.msgtype),
        )
        if index % 100 == 0:
            writer.write(
                mode,
                timestamp,
                store.serialize_ros1(string(data=f"mode-{index // 100}"), mode.msgtype),
            )

print(output)
