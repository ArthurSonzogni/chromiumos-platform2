#!/usr/bin/env python3

import logging
import timeit
import unittest

import numpy as np
import pandas as pd

import fpsutils


def smalltimestr(sec):
    s = int(sec)
    ms = int(sec * 1e3) % 1000
    us = int(sec * 1e6) % 1000
    ns = (sec * 1e9) % 1000

    string = s and f'{s}s' or ''
    string += ms and f'{ms}ms' or ''
    string += us and f'{us}us' or ''
    string += ns and f'{ns:3.3f}ns' or ''
    return string


def autorange(stmt, setup='pass', globals={**locals(), **globals()}):

    loops, sec = timeit.Timer(
        stmt,
        setup=setup,
        globals=globals).autorange()
    print(f'Ran "{stmt}" {loops} times over {sec}s.'
          ' '
          f'It took {smalltimestr(sec/loops)} per loop.')


class Test_boot_sample(unittest.TestCase):
    def test_benchmark(self):
        # self.assertEqual(inc_dec.increment(3), 4)

        print('# Running benchmarks for boot_sample')

        # Test the performance of boot_sampl.
        rng = np.random.default_rng()
        vals = rng.integers(1000000, size=100)
        # We would expect the first two tests to have similar execultion performance.
        vars = {**locals(), **globals()}
        autorange('fpsutils.boot_sample(vals, rng=rng)', globals=vars)
        autorange('fpsutils.boot_sample(vals, n=1, rng=rng)', globals=vars)
        autorange('fpsutils.boot_sample(vals, n=100, rng=rng)', globals=vars)


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
        autorange('s.isin((0, 0, 0, 0))',
                  globals={**locals(), **globals()}) # Not in set
        autorange('s.isin((400, 400, 400, 400))',
                  globals={**locals(), **globals()}) # In set

        # print((400, 400, 400, 400, True) in df.values)
        autorange('(400, 400, 400, 400, True) in df.values',
                  globals={**locals(), **globals()})
        autorange('(400, 400, 400, 400, True) in df.values',
                  globals={**locals(), **globals()})

        df = df.set_index(['A', 'B', 'C', 'D', 'E'])
        # print((400, 400, 400, 400, True) in df.values)
        autorange('(400, 400, 400, 400, True) in df.values',
                  globals={**locals(), **globals()})
        autorange('(400, 400, 400, 400, True) in df.values',
                  globals={**locals(), **globals()})


if __name__ == '__main__':
    unittest.main()
