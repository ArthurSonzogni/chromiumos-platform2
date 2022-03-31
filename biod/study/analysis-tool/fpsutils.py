#!/usr/bin/env python

import sys
import typing

import matplotlib.pyplot as plt
import numpy as np
import numpy.typing as nptyping
import pandas as pd
from IPython.display import Markdown, display
from scipy.stats import norm


class DataFrameSetAccess:
    '''Provides a quick method of checking if a given row exists in the table.

    This look method takes hundreds of nanoseconds vs other methods that take
    hudreds of micro seconds. Given the amount of times we must query certain
    tables, this order of magnitude difference is unacceptable.
    '''

    def __init__(self, table: pd.DataFrame, cols: list = None):

        if not cols:
            cols = table.columns

        # This is an expensive operation.
        self.set = {tuple(row) for row in np.array(table[cols])}
        # print(f'Cached set takes {sys.getsizeof(self.set)/1024.0}KB of memory.')

    def isin(self, values: tuple) -> bool:
        return values in self.set


def boot_sample(
    a,
    *,
    n: int = None,
    rng: np.random.Generator = np.random.default_rng()
) -> np.ndarray:
    """Sample with replacement the same number of elements given.

    If `n` is given, do `n` number of repeat bootstrap samples
    and return an `n x a.size` ndarray.

    Equivalent to `rng.choice(a, size=(a.size, n), replace=True)`.
    """

    if n:
        return rng.choice(a, size=(n, a.size), replace=True)
    else:
        return rng.choice(a, size=a.size, replace=True)


def discrete_hist(data, discrete_bins):
    # unique_elements = np.unique(data)
    bins = np.append(discrete_bins, np.max(discrete_bins) + 1)
    hist, bin_edges = np.histogram(data, bins=bins)
    return hist, discrete_bins


def plt_discrete_hist(data, bins=None):

    if bins:
        pass
    else:
        counts = np.bincount(data)

    # hist, bin_edges = np.histogram(grouped_counts, bins=np.arange(max_count+1+1))
    # We need to zoom in, since there would be thousands of thousands of bars that
    # are zero near the tail end.
    nonzero_indicies = np.nonzero(counts)
    first_index = np.min(nonzero_indicies)
    # first_index = 0
    last_index = np.max(nonzero_indicies)

    if (last_index-first_index) < 2000:
        # plt.title(f'Samples {np.size(r)} | p = {p:.3e} | Groups {groups}')
        # x = bin_edges[first_index:last_index+1]
        x = np.arange(start=first_index, stop=last_index+1)
        # h = hist[first_index:last_index+1]
        h = counts[first_index:last_index+1]
        plt.bar(x, h)
        # plt.xticks(x)
        # plt.xlabel('Group Sums')
        plt.ylabel('Frequency')
        # plt.vlines(bin_edges[:np.size(bin_edges)-1], 0, hist)

        # Overlay Norm Curve
        mu, std = norm.fit(data)
        mean = np.mean(data)
        print(f'first={first_index} last={last_index}')
        print(f'mu={mu} , std={std} 3*std={3*std}, np.mean(data) = {np.mean(data)}')

        # x_curve = x
        x_curve = np.linspace(mean - 3*std, mean + 3*std, 50)
        p = norm.pdf(x_curve, mu, std)
        p_scaled = p * np.sum(h)
        plt.plot(x_curve, p_scaled, 'k', linewidth=2)
        plt.xticks([mean - 3*std, mean, mean + 3*std])
    else:
        display(Markdown(
            f'Plot is too large (first={first_index} last={last_index}), not diplaying.'))

    # plt.hist(results)

    # # Overlay Norm Curve
    # mu, std = norm.fit(results)
    # mean = np.mean(results)
    # print(f'first={first_index} last={last_index}')
    # print(f'mu={mu} , std={std} 3*std={3*std}, np.mean(grouped_counts) = {np.mean(grouped_counts)}')

    # # x_curve = x
    # x_curve = np.linspace(mean - 3*std, mean + 3*std, 50)
    # p = norm.pdf(x_curve, mu, std)
    # p_scaled = p * np.sum(np.bincount(results))
    # plt.plot(x_curve, p_scaled, 'k', linewidth=2)

    # plt.show()


if __name__ == '__main__':
    import timeit

    import numpy as np

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

    def autorange(stmt, setup='pass'):
        loops, sec = timeit.Timer(
            stmt,
            setup=setup,
            globals=globals()).autorange()
        print(f'Ran "{stmt}" {loops} times over {sec}s.'
              ' '
              f'It took {smalltimestr(sec/loops)} per loop.')

    print('# Running benchmarks for boot_sample')

    # Test the performance of boot_sampl.
    _rng = np.random.default_rng()
    _vals = _rng.integers(1000000, size=100)
    # We would expect the first two tests to have similar execultion performance.
    autorange('boot_sample(_vals, rng=_rng)')
    autorange('boot_sample(_vals, n=1, rng=_rng)')
    autorange('boot_sample(_vals, n=100, rng=_rng)')
    del _vals
    del _rng
