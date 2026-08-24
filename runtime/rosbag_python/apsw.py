"""Minimal APSW compatibility layer for local, read-only rosbag2 databases.

RosSignalStudio only opens filesystem paths. The rosbags project uses APSW to
also support abstract remote paths; Python's standard sqlite3 module is enough
for our local portable workflow.
"""

from __future__ import annotations

import sqlite3
from typing import Any

Binding = object

SQLITE_OPEN_READONLY = 0x00000001
SQLITE_OPEN_READWRITE = 0x00000002
SQLITE_OPEN_CREATE = 0x00000004
SQLITE_OPEN_DELETEONCLOSE = 0x00000008
SQLITE_OPEN_EXCLUSIVE = 0x00000010
SQLITE_OPEN_URI = 0x00000040
SQLITE_OPEN_AUTOPROXY = 0x00000020
SQLITE_OPEN_WAL = 0x00080000
SQLITE_OPEN_SUPER_JOURNAL = 0x00004000


class VFSFile:
    def __init__(self, *_args: Any, **_kwargs: Any) -> None:
        raise RuntimeError("Virtual ROS bag paths are not supported")


class VFS:
    def __init__(self, *_args: Any, **_kwargs: Any) -> None:
        pass

    def xOpen(self, *_args: Any, **_kwargs: Any) -> VFSFile:
        raise RuntimeError("Virtual ROS bag paths are not supported")


URIFilename = str


class Connection:
    def __init__(self, filename: str, **_kwargs: Any) -> None:
        self._connection = sqlite3.connect(filename, uri=True)

    def cursor(self) -> sqlite3.Cursor:
        return self._connection.cursor()

    def execute(self, *args: Any, **kwargs: Any) -> sqlite3.Cursor:
        return self._connection.execute(*args, **kwargs)

    def close(self) -> None:
        self._connection.close()
