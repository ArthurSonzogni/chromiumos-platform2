# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for generating ChromeOS ConfigFS private data file."""

from __future__ import print_function

import io
import os
from pathlib import Path
import subprocess
import tarfile
from typing import Any, Dict, List, Union


def Serialize(obj: Any) -> bytes:
    """Convert a string, integer, bytes, or bool to its file representation.

    Args:
        obj: The string, integer, bytes, or bool to serialize.

    Returns:
        The bytes representation of the object suitable for dumping into a file.
    """
    if isinstance(obj, bytes):
        return obj
    if isinstance(obj, bool):
        return b"true" if obj else b"false"
    return str(obj).encode("utf-8")


def WriteConfigFSTar(
    config: Any,
    base_path: Union[str, Path],
    tar: tarfile.TarFile,
) -> None:
    """Recursive function to write ConfigFS data out to a tar stream.

    Args:
        config: The configuration item (dict, list, str, int, or bool).
        base_path: The path in the tar archive.
        tar: An open tarfile.TarFile instance.
    """
    if isinstance(config, dict):
        iterator = config.items()
    elif isinstance(config, list):
        iterator = enumerate(config)
    else:
        iterator = None

    if iterator is not None:
        ti = tarfile.TarInfo(name=str(base_path))
        ti.type = tarfile.DIRTYPE
        ti.mode = 0o755
        ti.mtime = 0
        tar.addfile(ti)
        base_str = str(base_path)
        for name, entry in iterator:
            path = f"{base_str}/{name}"
            WriteConfigFSTar(entry, path, tar)
    else:
        data = Serialize(config)
        ti = tarfile.TarInfo(name=str(base_path))
        ti.type = tarfile.REGTYPE
        ti.mode = 0o644
        ti.size = len(data)
        ti.mtime = 0
        tar.addfile(ti, io.BytesIO(data))


def GenerateConfigFSData(
    config: Union[Dict[str, Any], List[Any], Any],
    output_fs: Union[str, Path],
) -> None:
    """Generate the ConfigFS private data.

    Args:
        config: The configuration dictionary.
        output_fs: The file name to write the SquashFS image at.
    """
    with subprocess.Popen(
        [
            "mksquashfs",
            "-",
            str(output_fs),
            "-tar",
            "-no-xattrs",
            "-noappend",
            "-all-root",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    ) as proc:
        try:
            with tarfile.open(fileobj=proc.stdin, mode="w|") as tar:
                WriteConfigFSTar(config, "v1", tar)
        except BrokenPipeError:
            pass
        _, stderr = proc.communicate()
        if proc.returncode != 0:
            raise subprocess.CalledProcessError(
                proc.returncode, proc.args, stderr=stderr
            )
