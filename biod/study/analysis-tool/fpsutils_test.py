#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the fpsutils module."""

import time  # Used in benchmark unit test.
import unittest

import numpy as np
import pandas as pd

import fpsutils


class Test_boot_sample(unittest.TestCase):
    def test_benchmark(self):
        # self.assertEqual(inc_dec.increment(3), 4)

        print('# Running benchmarks for boot_sample')

        # Test the performance of boot_sample.
        rng = np.random.default_rng()
        vals = rng.integers(1000000, size=100)
        # We would expect the first two tests to have similar runtime
        # performance.
        vars = {**locals(), **globals()}
        fpsutils.benchmark('fpsutils.boot_sample(vals, rng=rng)',
                           globals=vars)
        fpsutils.benchmark('fpsutils.boot_sample(vals, n=1, rng=rng)',
                           globals=vars)
        fpsutils.benchmark('fpsutils.boot_sample(vals, n=100, rng=rng)',
                           globals=vars)


class Test_boot_sample_range(unittest.TestCase):
    def test_benchmark(self):
        # self.assertEqual(inc_dec.increment(3), 4)

        print('# Running benchmarks for boot_sample_range')

        # Test the performance of boot_sample_range.
        rng = np.random.default_rng()
        # These are equivalent calls and should be extremely close in runtime.
        fpsutils.benchmark(
            'fpsutils.boot_sample_range(100, rng=rng)',
            globals={**locals(), **globals()})
        fpsutils.benchmark('rng.choice(100, size=100)',
                           globals={**locals(), **globals()})


def generate_fake_decision_table() -> pd.DataFrame:
    return pd.DataFrame({
        # Column : Aligned Row Values
        'A': np.arange(600),
        'B': np.arange(600),
        'C': np.arange(600),
        'D': np.arange(600),
        # Simulated False Accept when True. Odd number is True.
        'E': [False, True]*300,
    })


class Test_DataFrameSetAccess(unittest.TestCase):
    def test_benchmark(self):
        df = generate_fake_decision_table()
        # Select the True rows and save only the columns A through B.
        # This equates to all number 300 through 599.
        fa_table = df.loc[df['E'] == True][['A', 'B', 'C', 'D']]
        s = fpsutils.DataFrameSetAccess(fa_table)

        # Not in set
        self.assertFalse(s.isin((0, 0, 0, 0)))
        fpsutils.benchmark('s.isin((0, 0, 0, 0))',
                           globals={**locals(), **globals()})

        # In set
        self.assertTrue(s.isin((501, 501, 501, 501)))
        fpsutils.benchmark('s.isin((501, 501, 501, 501))',
                           globals={**locals(), **globals()})

        print('# Try the alternative scheme of querying the DataFrame directly.')
        # print((df == [501, 501, 501, 501, True]).all(axis=1).any())
        self.assertTrue((df == [501, 501, 501, 501, True]).all(axis=1).any())
        fpsutils.benchmark('(df == (501, 501, 501, 501, True)).all(axis=1).any()',
                           globals={**locals(), **globals()})
        # This is faster, but deprecated.
        # self.assertTrue((501, 501, 501, 501, True) in df.values)
        # fpsutils.autorange('(501, 501, 501, 501, True) in df.values',
        #                    globals={**locals(), **globals()})

        # print('# Try querying after we set all columns as indicies.')
        # df = df.set_index(['A', 'B', 'C', 'D', 'E'])
        # I can't seem to get this to work.
        # print(df.isin((501, 501, 501, 501, True)).all(axis=1).any())
        # self.assertTrue((501, 501, 501, 501, True) in df.values)
        # print((501, 501, 501, 501, True) in df.values)
        # fpsutils.autorange('(501, 501, 501, 501, True) in df.values',
        #                    globals={**locals(), **globals()})
        # fpsutils.autorange('df.isin((501, 501, 501, 501, True)).all(axis=1).any()',
        #                    globals={**locals(), **globals()})
        # fpsutils.autorange('(df == (501, 501, 501, 501, True)).all(axis=1).any()',
        #                    globals={**locals(), **globals()})


class Test_DataFrameCountTrieAccess(unittest.TestCase):
    def test_benchmark(self):
        df = generate_fake_decision_table()
        # Select the True rows and save only the columns A through B.
        # This equates to all number 300 through 599.
        fa_table = df.loc[df['E'] == True][['A', 'B', 'C', 'D']]
        s = fpsutils.DataFrameCountTrieAccess(fa_table)

        # Not in set
        self.assertFalse(s.isin((0, 0, 0, 0)))
        fpsutils.benchmark('s.isin((0, 0, 0, 0))',
                           globals={**locals(), **globals()})

        # In set
        self.assertTrue(s.isin((501, 501, 501, 501)))
        fpsutils.benchmark('s.isin((501, 501, 501, 501))',
                           globals={**locals(), **globals()})
        self.assertGreater(s.counts(()), 1)
        self.assertEqual(s.counts((501,)), 1)
        self.assertEqual(s.counts((501, 501)), 1)
        self.assertEqual(s.counts((501, 501, 501)), 1)
        self.assertEqual(s.counts((501, 501, 501, 501)), 1)
        fpsutils.benchmark('s.isin((501, 501, 501, 501))',
                           globals={**locals(), **globals()})


class Test_Simple_Functions(unittest.TestCase):
    def test_elapsed_time_str_single_unit(self):
        self.assertEqual(fpsutils.elapsed_time_str(60.0**2), '1hr')
        self.assertEqual(fpsutils.elapsed_time_str(60.0), '1min')
        self.assertEqual(fpsutils.elapsed_time_str(0.500), '500ms')
        self.assertEqual(fpsutils.elapsed_time_str(0.0002), '200us')
        self.assertEqual(fpsutils.elapsed_time_str(0.0002/1000), '200.000ns')

    def test_elapsed_time_str_combined_units(self):
        self.assertEqual(
            fpsutils.elapsed_time_str(5*60.0**2 + 60 + 2),
            '5hr 1min 2s'
        )
        self.assertEqual(
            fpsutils.elapsed_time_str(
                5*60**2 + 60 + 2 + 34/1e3 + 67/1e6 + 999.87/1e9
            ),
            '5hr 1min 2s 34ms 67us 999.870ns'
        )

    def test_benchmark(self):
        SLEEP_TIME = 1.245
        TIME_EQ_DELTA = 0.100
        loops, sec, sec_per_loop = fpsutils.benchmark(
            'time.sleep(SLEEP_TIME)',
            globals={**locals(), **globals()}
        )

        self.assertGreater(loops, 0)
        self.assertGreaterEqual(sec, sec_per_loop)
        self.assertAlmostEqual(np.divide(sec, loops), sec_per_loop)
        self.assertAlmostEqual(sec_per_loop, SLEEP_TIME, delta=TIME_EQ_DELTA)


if __name__ == '__main__':
    unittest.main()
