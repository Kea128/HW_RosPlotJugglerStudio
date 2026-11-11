"""Regression tests for runtime/rosbag_python/extract_rosbag.py."""

from __future__ import annotations

import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RUNTIME = Path(os.environ["RSPJ_WORKER_RUNTIME"]).resolve()
WORKER = RUNTIME / "extract_rosbag.py"
sys.path.insert(0, str(RUNTIME))

try:
    import numpy as np
    from rosbags.interfaces import MessageDefinition, MessageDefinitionFormat
    from rosbags.rosbag1 import Reader as Ros1Reader
    from rosbags.rosbag1 import Writer as Ros1Writer
    from rosbags.rosbag2 import Reader as Ros2Reader
    from rosbags.rosbag2 import StoragePlugin
    from rosbags.rosbag2 import Writer as Ros2Writer
    from rosbags.typesys import Stores, get_typestore

    import extract_rosbag

    IMPORT_ERROR = None
except ImportError as error:
    IMPORT_ERROR = error

requires_worker = unittest.skipIf(
    IMPORT_ERROR is not None, f"ROS bag worker dependencies are unavailable: {IMPORT_ERROR}"
)


@requires_worker
class FlattenTest(unittest.TestCase):
    def test_keeps_numpy_bool_arrays(self) -> None:
        store = get_typestore(Stores.ROS2_HUMBLE)
        value_type = store.types["rcl_interfaces/msg/ParameterValue"]
        message = value_type(
            type=1,
            bool_value=True,
            integer_value=0,
            double_value=0.0,
            string_value="",
            byte_array_value=np.array([], dtype=np.uint8),
            bool_array_value=np.array([True, False], dtype=np.bool_),
            integer_array_value=np.array([], dtype=np.int64),
            double_array_value=np.array([], dtype=np.float64),
            string_array_value=[],
        )
        decoded = store.deserialize_cdr(
            store.serialize_cdr(message, value_type.__msgtype__), value_type.__msgtype__
        )

        records = {name: value for _, name, value in extract_rosbag.flatten(decoded, "/p", 100)}

        self.assertEqual(records["/p/bool_value"], 1.0)
        self.assertEqual(records["/p/bool_array_value[0]"], 1.0)
        self.assertEqual(records["/p/bool_array_value[1]"], 0.0)

    def test_limits_arrays_and_skips_non_finite_values(self) -> None:
        values = np.array([1.0, float("nan"), float("inf"), 4.0, 5.0])

        records = list(extract_rosbag.flatten(values, "/a", 4))

        self.assertEqual(records, [("N", "/a[0]", 1.0), ("N", "/a[3]", 4.0)])


def write_lagging_ros1_bag(path: Path, compression: str) -> None:
    """Two topics whose stamps differ by 0.5 s, so consecutive chunks overlap in time."""
    store = get_typestore(Stores.ROS1_NOETIC)
    float64 = store.types["std_msgs/msg/Float64"]
    writer = Ros1Writer(path)
    if compression != "none":
        writer.set_compression(Ros1Writer.CompressionFormat[compression.upper()])
    writer.chunk_threshold = 4096
    with writer:
        live = writer.add_connection("/live", "std_msgs/msg/Float64", typestore=store)
        late = writer.add_connection("/late", "std_msgs/msg/Float64", typestore=store)
        for index in range(600):
            stamp = 1_000_000_000_000 + index * 10_000_000
            payload = store.serialize_ros1(float64(data=float(index)), live.msgtype)
            writer.write(live, stamp, payload)
            writer.write(late, stamp - 500_000_000, payload)


@requires_worker
class Ros1MessageOrderTest(unittest.TestCase):
    def test_chunk_cache_matches_reader_messages(self) -> None:
        for compression in ("none", "bz2", "lz4"):
            with self.subTest(compression=compression), tempfile.TemporaryDirectory() as directory:
                bag = Path(directory) / "lagging.bag"
                write_lagging_ros1_bag(bag, compression)
                with Ros1Reader(bag) as reader:
                    self.assertGreater(len(reader.chunk_infos), 10)
                    connections = [c for c in reader.connections if c.topic in {"/live", "/late"}]
                    expected = [
                        (c.id, t, bytes(d)) for c, t, d in reader.messages(connections=connections)
                    ]
                    actual = [
                        (c.id, t, bytes(d))
                        for c, t, d in extract_rosbag.iter_ros1_messages(reader, connections)
                    ]
                self.assertEqual(len(actual), 1200)
                self.assertEqual(actual, expected)


@requires_worker
class Ros2SchemaTest(unittest.TestCase):
    def test_uses_bag_definitions_and_falls_back_to_type_library(self) -> None:
        store = get_typestore(Stores.ROS2_HUMBLE)
        float64 = store.types["std_msgs/msg/Float64"]
        with tempfile.TemporaryDirectory() as directory:
            bag = Path(directory) / "bag"
            with Ros2Writer(bag, version=9, storage_plugin=StoragePlugin.SQLITE3) as writer:
                connection = writer.add_connection("/speed", "std_msgs/msg/Float64", typestore=store)
                writer.write(connection, 1, store.serialize_cdr(float64(data=1.0), connection.msgtype))
            with Ros2Reader(bag) as reader:
                stored = reader.connections[0]
        self.assertEqual(stored.msgdef.format, MessageDefinitionFormat.MSG)

        schemas = extract_rosbag.Ros2Schemas()
        self.assertEqual(schemas.schema(stored), stored.msgdef.data)
        # Bags recorded before ROS 2 Iron carry no definitions.
        undefined = stored._replace(msgdef=MessageDefinition(MessageDefinitionFormat.NONE, ""))
        self.assertIn("float64 data", schemas.schema(undefined))
        idl = stored._replace(msgdef=MessageDefinition(MessageDefinitionFormat.IDL, "module x {};"))
        self.assertEqual(schemas.schema(idl), "")

    def test_opens_bags_with_idl_and_unknown_definitions(self) -> None:
        store = get_typestore(Stores.ROS2_HUMBLE)
        float64 = store.types["std_msgs/msg/Float64"]
        string = store.types["std_msgs/msg/String"]
        idl = "module custom_msgs { module msg { struct Reading { double value; }; }; };\n"
        with tempfile.TemporaryDirectory() as directory:
            bag = Path(directory) / "bag"
            with Ros2Writer(bag, version=9, storage_plugin=StoragePlugin.SQLITE3) as writer:
                speed = writer.add_connection("/speed", "std_msgs/msg/Float64", typestore=store)
                label = writer.add_connection("/label", "std_msgs/msg/String", typestore=store)
                writer.add_connection(
                    "/reading",
                    "custom_msgs/msg/Reading",
                    msgdef="=" * 80 + "\nIDL: custom_msgs/msg/Reading\n" + idl,
                    rihs01="RIHS01_" + "0" * 64,
                )
                writer.write(speed, 1, store.serialize_cdr(float64(data=1.0), speed.msgtype))
                writer.write(label, 2, store.serialize_cdr(string(data="a"), label.msgtype))
            # What rosbag2 stores when the recorder cannot find a definition.
            with sqlite3.connect(next(bag.glob("*.db3"))) as database:
                database.execute(
                    "UPDATE message_definitions SET encoding = 'unknown',"
                    " encoded_message_definition = '' WHERE topic_type = 'std_msgs/msg/String'"
                )
            database.close()

            with Ros2Reader(bag) as reader:
                formats = {c.topic: c.msgdef.format for c in reader.connections}
                schemas = extract_rosbag.Ros2Schemas()
                generated = {c.topic: schemas.schema(c) for c in reader.connections}

        self.assertEqual(formats["/speed"], MessageDefinitionFormat.MSG)
        self.assertEqual(formats["/reading"], MessageDefinitionFormat.IDL)
        self.assertEqual(formats["/label"], MessageDefinitionFormat.NONE)
        self.assertIn("string data", generated["/label"])
        self.assertEqual(generated["/reading"], "")


@requires_worker
class InspectEncodingTest(unittest.TestCase):
    def test_topic_index_is_utf8_regardless_of_console_code_page(self) -> None:
        store = get_typestore(Stores.ROS1_NOETIC)
        float64 = store.types["std_msgs/msg/Float64"]
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        environment.pop("PYTHONIOENCODING", None)
        environment.pop("PYTHONUTF8", None)

        with tempfile.TemporaryDirectory() as directory:
            bag = Path(directory) / "comments.bag"
            with Ros1Writer(bag) as writer:
                connection = writer.add_connection(
                    "/vehicle/speed",
                    "std_msgs/msg/Float64",
                    msgdef="# \u8f66\u901f\uff0c\u5355\u4f4d m/s\nfloat64 data\n",
                    md5sum="fdb28210bfa9d7c91146260178d9a584",
                )
                writer.write(
                    connection, 1, store.serialize_ros1(float64(data=1.0), connection.msgtype)
                )
            result = subprocess.run(
                [sys.executable, "-u", str(WORKER), str(bag), "--inspect"],
                capture_output=True,
                env=environment,
                check=False,
                timeout=120,
            )

        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
        index = json.loads(result.stdout.decode("utf-8"))
        self.assertIn("\u8f66\u901f", index["topics"][0]["schema"])


if __name__ == "__main__":
    unittest.main()
