#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import glob
import pathlib
import tempfile
import time
import unittest

from cached_data_file import CachedCSVFile
import pandas as pd
import simulate_fpstudy


class Test_CachedCSVFile(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.temp_csv = pathlib.Path(self.temp_dir.name) / "test.csv"
        print(f"Temp CSV file is {self.temp_csv}.")
        self._generate_decisions_file()

        return super().setUp()

    def tearDown(self) -> None:
        self.temp_dir.cleanup()
        return super().tearDown()

    def _generate_decisions_file(self):
        # VerifyUser VerifyFinger VerifySample EnrollUser EnrollFinger Decision
        # This can take about 2 seconds.
        self.far_decisions = simulate_fpstudy.GenerateFARResults(
            num_users=72,
            num_fingers=3,
            num_verify_samples=40,
            user_groups=None,
            verbose=False,
        )
        self.far_decisions.to_csv(self.temp_csv, index=False)

    def _setup_two_cache_files(self):
        """Setup the directory with two cache files.

        This requires the csv file to change once and a new cache file be
        generated.
        """
        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            _ = c.get()
        self._generate_decisions_file()
        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            _ = c.get()

        pickles = glob.glob(str(pathlib.Path(self.temp_dir.name) / "*.pickle"))
        self.assertEqual(len(pickles), 2)

    def test_benchmark(self):
        # Get timing for running the alternative pd.read_csv.
        time_original_start = time.time()
        _ = pd.read_csv(self.temp_csv)
        time_original_end = time.time()

        # Get timing for running just the parser of CachedCSVFile.
        # This asseses the overhead of the wrapper.
        time_parse_nocache_start = time.time()
        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            _ = c.get(disable_cache=True)
        time_parse_nocache_end = time.time()

        # Get timing of hashing, parsing and saving cache.
        time_parse_start = time.time()
        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            _ = c.get()
        time_parse_end = time.time()

        # Get timing of hashing, and using cached version.
        time_load_start = time.time()
        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            _ = c.get()
        time_load_end = time.time()

        delta_original = time_original_end - time_original_start
        delta_parse_nocache = time_parse_nocache_end - time_parse_nocache_start
        delta_parse = time_parse_end - time_parse_start
        delta_load = time_load_end - time_load_start

        print(f"The original pd.read_csv took {delta_original}s.")
        print(f"It took {delta_parse_nocache}s to parse.")
        print(f"It took {delta_parse}s to hash, parse, and save cache.")
        print(f"It took {delta_load}s to hash and load from cache.")
        print("SUMMARY:")
        slowdown = delta_parse / delta_original
        speedup = delta_original / delta_load
        print(
            f"For an initial {slowdown:.3f} slowdown, "
            f"we see an {speedup:.3f} speedup for using cache."
        )

    def test_basic(self):
        # Get timing for running the alternative pd.read_csv.
        original = pd.read_csv(self.temp_csv)

        # Compare output of just running the parser without caching.
        with CachedCSVFile(self.temp_csv, verbose=True) as c:
            parsed_nocache = c.get(disable_cache=True)

        self.assertTrue(original.equals(parsed_nocache))

        # Compare output of hashing, parsing and saving cache.
        with CachedCSVFile(self.temp_csv, verbose=True) as c:
            parsed = c.get()

        self.assertTrue(original.equals(parsed))

        # Compare output of hashing, and using cached version.
        with CachedCSVFile(self.temp_csv, verbose=True) as c:
            loaded = c.get()

        self.assertTrue(original.equals(loaded))

    def test_reparse(self):
        """Ensure that if the data file changes that we reparse and recache."""
        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            first = c.get()

        pickles = glob.glob(str(pathlib.Path(self.temp_dir.name) / "*.pickle"))
        self.assertEqual(len(pickles), 1)

        self._generate_decisions_file()
        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            second = c.get()

        pickles = glob.glob(str(pathlib.Path(self.temp_dir.name) / "*.pickle"))
        self.assertEqual(len(pickles), 2)

        self.assertFalse(first.equals(second))

    def test_prune(self):
        self._setup_two_cache_files()

        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            c.prune()

        pickles = glob.glob(str(pathlib.Path(self.temp_dir.name) / "*.pickle"))
        self.assertEqual(len(pickles), 1)

    def test_prune_shred(self):
        self._setup_two_cache_files()

        with CachedCSVFile(self.temp_csv, verbose=True) as c:
            c.prune(rm_cmd="shred", rm_cmd_opts=["-v", "-u"])

        pickles = glob.glob(str(pathlib.Path(self.temp_dir.name) / "*.pickle"))
        self.assertEqual(len(pickles), 1)

    def test_remove(self):
        self._setup_two_cache_files()

        with CachedCSVFile(self.temp_csv, verbose=False) as c:
            c.remove()

        pickles = glob.glob(str(pathlib.Path(self.temp_dir.name) / "*.pickle"))
        self.assertEqual(len(pickles), 0)


if __name__ == "__main__":
    unittest.main()
