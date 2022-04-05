#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the fpsutils module."""

import unittest

import numpy as np
import pandas as pd

import fpsutils


class Test_boot_sample(unittest.TestCase):
    def test_benchmark(self):
        # self.assertEqual(inc_dec.increment(3), 4)

        print('# Running benchmarks for boot_sample')

        # Test the performance of boot_sampl.
        rng = np.random.default_rng()
        vals = rng.integers(1000000, size=100)
        # We would expect the first two tests to have similar execultion performance.
        vars = {**locals(), **globals()}
        fpsutils.autorange('fpsutils.boot_sample(vals, rng=rng)',
                           globals=vars)
        fpsutils.autorange('fpsutils.boot_sample(vals, n=1, rng=rng)',
                           globals=vars)
        fpsutils.autorange('fpsutils.boot_sample(vals, n=100, rng=rng)',
                           globals=vars)


class Test_DataFrameSetAccess(unittest.TestCase):
    def test_benchmark(self):
        df = pd.DataFrame({
            # Column : Aligned Row Values
            'A': np.arange(600),
            'B': np.arange(600),
            'C': np.arange(600),
            'D': np.arange(600),
            'E': [False]*300 + [True]*300,  # Simulated False Accept when True.
        })
        print(df)

        # Select the True rows and save only the columns A through B.
        # This equates to all number 300 through 599.
        fa_table = df.loc[df['E'] == True][['A', 'B', 'C', 'D']]
        # print(fa_table)
        s = fpsutils.DataFrameSetAccess(fa_table)
        fpsutils.autorange('s.isin((0, 0, 0, 0))',
                           globals={**locals(), **globals()})  # Not in set
        fpsutils.autorange('s.isin((400, 400, 400, 400))',
                           globals={**locals(), **globals()})  # In set

        # print((400, 400, 400, 400, True) in df.values)
        fpsutils.autorange('(400, 400, 400, 400, True) in df.values',
                           globals={**locals(), **globals()})
        fpsutils.autorange('(400, 400, 400, 400, True) in df.values',
                           globals={**locals(), **globals()})

        df = df.set_index(['A', 'B', 'C', 'D', 'E'])
        # print((400, 400, 400, 400, True) in df.values)
        fpsutils.autorange('(400, 400, 400, 400, True) in df.values',
                           globals={**locals(), **globals()})
        fpsutils.autorange('(400, 400, 400, 400, True) in df.values',
                           globals={**locals(), **globals()})


if __name__ == '__main__':
    unittest.main()
