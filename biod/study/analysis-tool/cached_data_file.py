# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Provides cached/fast access to data backed by files."""

from __future__ import annotations

import fcntl
import glob
import hashlib
import mmap
import pathlib
import pickle
import subprocess
from typing import Any

import pandas as pd


class CachedDataFile:
    """A data file parsing handler that will automatically cache the results.

    This handler implementation is thread safe through the use of a lock on
    the source data file.
    """

    def _cache_file_path(self, ver: str) -> pathlib.Path:
        """Build the path to the file's cache file with the given version.

        In the event that we are caching sensitive data, the cache fil should
        be co-located near the original file to ensure the same backup
        policies and storage medium are used.
        """
        orig_path = self._orig_file_path
        name_parts = orig_path.name.split(".")
        assert len(name_parts) >= 2
        # <parent_dirs>/<basename>-<ver>.<extensions...>.pickle
        name = (
            name_parts[0]
            + "-"
            + ver
            + "."
            + ".".join(name_parts[1:])
            + ".pickle"
        )
        return orig_path.parent / name

    def _cache_file_pattern(self) -> str:
        """Build the glob pattern to find cache files for all file versions."""
        return str(self._cache_file_path("*"))

    def _find_cache_files(self) -> list[pathlib.Path]:
        """Return a list of all cache files for the data file."""
        cache_files = glob.glob(self._cache_file_pattern())
        return [pathlib.Path(p) for p in cache_files]

    def __init__(
        self, data_file_path: pathlib.Path | str, verbose: bool = False
    ) -> None:
        self._ver = ""
        self._orig_file_path = pathlib.Path(data_file_path)
        self._verbose = verbose

    def open(self):
        self._f = open(self._orig_file_path, "rb")
        # We lock the original data file to ensure that we will not collide
        # with another instance of this object when listing/reading/writing
        # cache files.
        fcntl.flock(self._f, fcntl.LOCK_EX)
        # Memory map the entire file to serve as a file cache between
        # reading for the checksum and reading for parsing.
        # Size 0 means the whole file.
        self._mm = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)

    def close(self):
        self._mm.close()
        self._f.close()

    def version(self) -> str:
        """Return the md5 checksum of the file as a version string."""
        if not self._ver:
            self._ver = hashlib.md5(self._mm).hexdigest()
        return self._ver

    def _delete_file(
        self,
        file: pathlib.Path,
        rm_cmd: str | None = None,
        rm_cmd_opts: list[str] | None = None,
    ):
        """Delete a cache file.

        This can be used to simply `unlink` the file or invoke another removal
        program to wipe out the contents.

        Example Args:
            rm_cmd = 'shred'
            rm_cmd_opts = ['-v', '-u']

        WARNING: I do not believe the `shred` command with `-u` is thread-safe.
                 This is because it also shreds the file name, which relies on
                 it repeatedly renaming the file to a common name of all 0's.
                 I believe these names will collide if being done on more than
                 one file simultaneously.
        """
        if self._verbose:
            print(f"Removing {file}.")
        if rm_cmd:
            cmd = [rm_cmd]
            if rm_cmd_opts:
                cmd.extend(rm_cmd_opts)
            cmd.append(str(file))
            if self._verbose:
                print(f"Running {cmd}.")
            s = subprocess.run(
                cmd,
                stderr=subprocess.STDOUT,
            )
            s.check_returncode()
        else:
            file.unlink()

    def prune(
        self,
        rm_cmd: str | None = None,
        rm_cmd_opts: list[str] | None = None,
    ):
        """Remove obsolete cache files."""
        ver = self.version()
        cached_file_path = self._cache_file_path(ver)
        cache_files = self._find_cache_files()

        for cache_file in set(cache_files) - {cached_file_path}:
            self._delete_file(cache_file, rm_cmd, rm_cmd_opts)

    def remove(
        self,
        rm_cmd: str | None = None,
        rm_cmd_opts: list[str] | None = None,
    ):
        """Remove all cache files."""
        for cache_file in self._find_cache_files():
            self._delete_file(cache_file, rm_cmd, rm_cmd_opts)

    def _parse(self) -> Any:
        """Parse the original file into a usable object.

        Implement me.
        """
        return None

    def get(self, disable_cache: bool = False) -> Any:
        """Get the parsed data using the caching mechanism."""
        if disable_cache:
            return self._parse()
        else:
            cached_file_path = self._cache_file_path(self.version())
            if cached_file_path in self._find_cache_files():
                if self._verbose:
                    print(f"Using cache file {cached_file_path}.")
                with open(cached_file_path, "rb") as f:
                    return pickle.load(f)
            else:
                parsed = self._parse()
                if self._verbose:
                    print(f"Saving to cache file {cached_file_path}.")
                with open(cached_file_path, "wb") as f:
                    # Might want pickle.HIGHEST_PROTOCOL arg.
                    pickle.dump(parsed, f)
                return parsed

    def __enter__(self):
        """Allow for the use of the python `with` statement."""
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Allow for the use of the python `with` statement."""
        self.close()


class CachedCSVFile(CachedDataFile):
    """Persistently cache the results of parsing a particular CSV file.

    This will save the parsed output from `pd.read_csv` in pickled format
    to a file. Upon reinstantiating a CachedCSFFile for a file whose hash
    matches that of the original parsed file, the cached version will
    be used, instead of reparsing.
    """

    def __init__(
        self,
        data_file_path: pathlib.Path | str,
        verbose: bool = False,
        **read_csv_args: Any,
    ) -> None:
        self._read_csv_arg = read_csv_args
        super().__init__(data_file_path=data_file_path, verbose=verbose)

    def _parse(self) -> Any:
        """Parse the CSV file into a pandas DataFrame."""
        return pd.read_csv(self._mm, **self._read_csv_arg)
