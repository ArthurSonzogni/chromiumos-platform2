#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import fcntl
import glob
import hashlib
import mmap
import pathlib
import pickle
from typing import Any, List, Union

import pandas as pd


class CachedDataFile:
    """A data file parsing handler that will automatically cache the results.

    This handler implementation is thread safe through the use of a lock on
    the source data file.
    """

    def _cache_file_path(self, ver: str) -> pathlib.Path:
        """Build the path to the file's cache file with the given version."""
        orig_path = self._orig_file_path
        name_parts = orig_path.name.split('.')
        assert len(name_parts) >= 2
        # <parent_dirs>/<basename>-<ver>.<extensions...>.pickle
        name = name_parts[0] + '-' + ver + '.' + \
            '.'.join(name_parts[1:]) + '.pickle'
        return orig_path.parent / name

    def _cache_file_pattern(self) -> str:
        """Build the glob pattern to find cache files for all file versions."""
        return str(self._cache_file_path('*'))

    def _find_cache_files(self) -> List[pathlib.Path]:
        """Return a list of all cache files for the data file."""
        cache_files = glob.glob(self._cache_file_pattern())
        return [pathlib.Path(p) for p in cache_files]

    def __init__(self,
                 data_file_path: Union[pathlib.Path, str],
                 verbose: bool = False) -> None:
        self._ver = None
        self._orig_file_path = pathlib.Path(data_file_path)
        self._verbose = verbose

    def open(self):
        self._f = open(self._orig_file_path, 'rb')
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

    def prune(self):
        """Remove obsolete cache files."""
        ver = self.version()
        cached_file_path = self._cache_file_path(ver)
        cache_files = self._find_cache_files()

        for cache_file in (set(cache_files) - {cached_file_path}):
            cache_file.unlink()

    def remove(self):
        """Remove all cache files."""
        for cache_file in self._find_cache_files():
            cache_file.unlink()

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
                    print(f'Using cache file {cached_file_path}.')
                with open(cached_file_path, 'rb') as f:
                    return pickle.load(f)
            else:
                parsed = self._parse()
                if self._verbose:
                    print(f'Saving to cache file {cached_file_path}.')
                with open(cached_file_path, 'wb') as f:
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
    def __init__(self,
                 data_file_path: Union[pathlib.Path, str],
                 verbose: bool = False,
                 **read_csv_args) -> None:
        self._read_csv_arg = read_csv_args
        return super().__init__(data_file_path=data_file_path, verbose=verbose)

    def _parse(self) -> Any:
        """Parse the CSV file into a pandas DataFrame."""
        return pd.read_csv(self._mm, **self._read_csv_arg)
