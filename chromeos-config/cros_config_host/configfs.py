# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for generating ChromeOS ConfigFS private data file."""

from __future__ import print_function

import io
from pathlib import Path
import subprocess
import tarfile
from typing import Any, Dict, List, Optional, Union


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
    seen_leaves: Optional[Dict[bytes, str]] = None,
) -> None:
    """Recursive function to write ConfigFS data out to a tar stream.

    Args:
        config: The configuration item (dict, list, str, int, or bool).
        base_path: The path in the tar archive.
        tar: An open tarfile.TarFile instance.
        seen_leaves: Mapping from serialized leaf bytes to the most recently
            written tar path for hardlink deduplication.
    """
    if seen_leaves is None:
        seen_leaves = {}

    if isinstance(config, dict):
        iterator = config.items()
    elif isinstance(config, list):
        iterator = enumerate(config)
    else:
        iterator = None

    base_str = str(base_path)
    if iterator is not None:
        ti = tarfile.TarInfo(name=base_str)
        ti.type = tarfile.DIRTYPE
        ti.mode = 0o755
        ti.mtime = 0
        tar.addfile(ti)
        for name, entry in iterator:
            path = f"{base_str}/{name}"
            WriteConfigFSTar(entry, path, tar, seen_leaves)
    else:
        data = Serialize(config)
        ti = tarfile.TarInfo(name=base_str)
        ti.mode = 0o644
        ti.mtime = 0
        # Deduplicate identical leaf files via hardlinks so repeated values
        # across thousands of SKUs share a single SquashFS inode (unlike
        # symlinks, hardlinks remain regular files when a single SKU directory
        # is bind-mounted at runtime).
        target = seen_leaves.get(data)
        if target is not None:
            ti.type = tarfile.LNKTYPE
            ti.linkname = target
            ti.size = 0
            tar.addfile(ti)
        else:
            ti.type = tarfile.REGTYPE
            ti.size = len(data)
            tar.addfile(ti, io.BytesIO(data))
        # Point subsequent links at the most recent path because mksquashfs -tar
        # prepends new directory entries to the head of its entry list.
        seen_leaves[data] = base_str


def GenerateConfigFSData(
    config: Union[Dict[str, Any], List[Any], Any],
    output_fs: Union[str, Path],
) -> None:
    """Generate the ConfigFS private data.

    Args:
        config: The configuration dictionary.
        output_fs: The file name to write the SquashFS image at.
    """
    # Use zstd with 1 MiB blocks and omit NFS export tables to shrink the
    # read-only image on multi-SKU boards (CONFIG_SQUASHFS_ZSTD is enabled in
    # ChromeOS base kernel configs).
    with subprocess.Popen(
        [
            "mksquashfs",
            "-",
            str(output_fs),
            "-tar",
            "-comp",
            "zstd",
            "-b",
            "1M",
            "-no-exports",
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
