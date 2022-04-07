#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities needed for Fingerprint Study Analysis."""

import timeit
from typing import List, Optional, Tuple, Union

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from IPython.display import Markdown, display
from scipy.stats import norm


class DataFrameSetAccess:
    """Provides a quick method of checking if a given row exists in the table.

    This look method takes hundreds of nanoseconds vs other methods that take
    hudreds of micro seconds. Given the amount of times we must query certain
    tables, this order of magnitude difference is unacceptable.
    """

    def __init__(self, table: pd.DataFrame, cols: List[str] = None):

        if not cols:
            cols = list(table.columns)
        self.cols = cols

        # There is nothing wrong with having duplicate rows for this
        # implementation, but having duplicates might indicate that we are
        # attempting to analyze cross sections of data using the wrong tool.
        # Just take for example that we are caching only the first three columns
        # of a dataframe. We really migt need to know the number of rows that
        # happen to have a particular three starting values, but this accessor
        # will only say one exists.
        assert not table.duplicated(subset=cols).any()

        # This is an expensive operation.
        self.set = {tuple(row) for row in np.array(table[cols])}

    def isin(self, values: tuple) -> bool:
        return values in self.set


class DataFrameCountTrieAccess:
    """Provides a quick method of checking the number of matching rows.

    This implementation builds a trie with all partial row columns,
    from the empty tuple to the full row tuple. At each node, the count
    of all downstream nodes is saved.

    This is on par with the performance of `DataFrameSetAccess`, but still
    tens of nanoseconds slower.
    """

    def __init__(self, table: pd.DataFrame, cols: List[str] = None):
        """This is an expensive caching operation."""

        if not cols:
            cols = list(table.columns)
        self.cols = cols

        self.counts_dict = dict()

        for row in np.array(table[cols]):
            for i in range(len(cols)+1):
                # We include the empty tuple (row[0:0]) count also.
                t = tuple(row)[0:i]
                self.counts_dict[t] = self.counts_dict.get(t, 0) + 1

    def isin(self, values: tuple) -> bool:
        """A tuple will only be in the cache if the count is at least 1."""
        return values in self.counts_dict

    def counts(self, values: tuple) -> int:
        """Get the number of rows that start with `values` tuple."""
        return self.counts_dict.get(values, 0)


def boot_sample(
    # This is the fastest input to rng.choice, other than a scalar.
    a: np.array,
    *,
    n: Optional[int] = None,
    rng: np.random.Generator = np.random.default_rng()
) -> np.ndarray:
    """Sample with replacement the same number of elements given.

    If `n` is given, do `n` number of repeat bootstrap samples
    and return an `n x a.size` ndarray.

    Equivalent to `rng.choice(a, size=(a.size, n), replace=True)`.

    NOTE: It is slightly faster when invoking rng.choice with a scalar as the
          first argument, instead of giving it an np.array.
          See `boot_sample_range`.
    """

    if n:
        return rng.choice(a, size=(n, a.size), replace=True)
    else:
        return rng.choice(a, size=a.size, replace=True)


def boot_sample_range(
    # Scalar input is the fastest invocation to rng.choice.
    range: int,
    n: Optional[int] = None,
    rng: np.random.Generator = np.random.default_rng()
) -> np.ndarray:
    """Sample with replacement `range` elements from `0` to `range`.

    This is slightly faster than `fpsutils.boot_sample`.

    Equivalent to `rng.choice(range, size=range, replace=True)`.
    """

    return rng.choice(range, range, replace=True)


def plot_pd_hist_discrete(tbl: pd.DataFrame,
                          title_prefix: str = None,
                          figsize: tuple = None):
    """Plot the histograms of a DataFrame columns.

    This is different than `pd.DataFrame.hist`, because it ensures that all
    unique elements of the column are represented in a bar plot. Other
    implementations will try to bin multiple values and doesn't center the
    graphical bars on the values.
    """

    num_plots = len(tbl.columns)
    if not figsize:
        figsize = (10, 6*num_plots)

    plt.figure(figsize=figsize)
    for index, col in enumerate(tbl.columns):
        plt.subplot(num_plots, 1, index+1)
        vals = np.unique(tbl[col], return_counts=True)
        plt.bar(*vals)
        plt.xticks(vals[0], rotation='vertical', fontsize=5)
        plt.xlabel(col)
        plt.ylabel('Count')
        if title_prefix:
            plt.title(f'{title_prefix} by {col}')
    plt.show()


def discrete_hist(data) -> Tuple[np.array, np.array]:
    """Return a tuple of unique items and their counts.

    Returns:
        ([items], [counts])
    """

    return np.unique(data, return_counts=True)


def plt_discrete_hist(data):

    counts = np.bincount(data)

    # We need to zoom in, since there would be thousands of thousands of bars that
    # are zero near the tail end.
    nonzero_indicies = np.nonzero(counts)
    first_index = np.min(nonzero_indicies)
    # first_index = 0
    last_index = np.max(nonzero_indicies)

    if (last_index-first_index) < 2000:
        # plt.title(f'Samples {np.size(r)} | p = {p:.3e} | Groups {groups}')

        x = np.arange(start=first_index, stop=last_index+1)
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
            f'Plot is too large (first={first_index} last={last_index}),'
            ' not diplaying.'
        ))


def plt_discrete_hist2(data):
    vals, counts = np.unique(data, return_counts=True)
    plt.bar(vals, counts)
    plt.xticks(vals, rotation='vertical', fontsize=5)
    # plt.xlabel(col)
    plt.ylabel('Count')
    # plt.show()

    # Overlay Norm Curve
    mu, std = norm.fit(data)
    mean = np.mean(data)
    # print(f'first={first_index} last={last_index}')
    print(f'mu={mu} , std={std} 3*std={3*std}, np.mean(data) = {np.mean(data)}')

    x_curve = np.linspace(mean - 3*std, mean + 3*std, 50)
    p = norm.pdf(x_curve, mu, std)
    p_scaled = p * np.sum(counts)
    plt.plot(x_curve, p_scaled, 'k', linewidth=2)
    mid_y = np.max(counts) / 2
    for ci_x, ci_label in [(mean - 2*std, 'lower 2*std'),
                           (mean, 'mean'),
                           (mean + 2*std, 'upper 2*std')]:
        plt.axvline(ci_x, color='red')
        plt.text(ci_x + 0.2,
                 mid_y,
                 f'{ci_x:.3f} is {ci_label}',
                 rotation=90,
                 color='red')


def elapsed_time_str(sec: float) -> str:
    """Convert a seconds value into a more easily interpretable units str.

    Example: elapsed_time_str(0.003) -> "3ms"
    """

    hour = int(sec / 60.0**2)
    sec -= hour * 60**2
    min = int(sec / 60.0)
    sec -= min * 60.0
    s = int(sec)
    ms = int(sec * 1e3) % 1000
    us = int(sec * 1e6) % 1000
    ns = (sec * 1e9) % 1000

    string = ''
    string += hour and f'{hour}hr ' or ''
    string += min and f'{min}min ' or ''
    string += s and f'{s}s ' or ''
    string += ms and f'{ms}ms ' or ''
    string += us and f'{us}us ' or ''
    string += ns and f'{ns:3.3f}ns ' or ''
    return string.rstrip()


def benchmark(stmt: str,
              setup: str = 'pass',
              globals: dict = {**locals(), **globals()}) \
        -> Tuple[int, float, float]:
    """Measure the runtime of `stmt`.

    This method invokes timeit.Timer.autorange and print results.

    Returns:
        (num_loops, sec_total, sec_per_loop)
    """

    loops, sec = timeit.Timer(
        stmt,
        setup=setup,
        globals=globals).autorange()
    print(f'Ran "{stmt}" {loops} times over {sec}s.'
          ' '
          f'It took {elapsed_time_str(sec/loops)} per loop.')
    return loops, sec, np.divide(sec, loops)
