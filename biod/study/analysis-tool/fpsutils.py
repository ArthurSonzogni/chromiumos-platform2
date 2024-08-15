#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities needed for Fingerprint Study Analysis."""

from __future__ import annotations

from collections import Counter
from enum import Enum
import timeit
from typing import Any, Iterable, Literal, Optional, Union

from IPython.display import display
from IPython.display import Markdown
import matplotlib.pyplot as plt
import numpy as np
import numpy.typing as npt
import pandas as pd
from scipy.stats import norm


class DataFrameSetAccess:
    """Provides a quick method of checking if a given row exists in the table.

    This look method takes hundreds of nanoseconds vs other methods that take
    hundreds of micro seconds. Given the amount of times we must query certain
    tables, this order of magnitude difference is unacceptable.

    The constructor is very slow, as it builds the cache, but all accessor
    methods are very fast.
    """

    def __init__(self, table: pd.DataFrame, cols: Optional[list[str]] = None):
        """This performs the expensive caching operation, that must occur once.

        Args:
            table: The DataFrame to cache.
            cols: The specific columns of the DataFrame to index into the Trie.
                If None, all columns are used.
        """
        if not cols:
            cols = list(table.columns)
        self.cols = cols

        # There is nothing incompatible with having duplicate rows for this
        # data structure, but being providing duplicates might indicate an
        # attempt to analyze cross sections of data using the wrong tool.
        # Just take for example that we are caching only the first three columns
        # of a DataFrame. If this DataFrame contains match decisions, then there
        # will be many rows that have the same first three columns. This data
        # structure will collapse all of these identical results to one entry.
        assert not table.duplicated(subset=cols).any()

        # This is an expensive operation.
        self.set = {tuple(row) for row in np.array(table[cols])}

    def isin(self, values: tuple[Any, ...]) -> bool:
        """Check if the values appeared as a cached row."""
        # This must remain very fast, so do not add additional asserts/check.
        return values in self.set


class DataFrameCountTrieAccess:
    """Provides a quick method of checking the number of matching rows.

    This implementation builds a trie with all partial row columns,
    from the empty tuple (all rows) to the full row in tuple form.
    The count of all downstream nodes is saved at each trie node.

    The constructor is very slow, as it builds the cache, but all accessor
    methods are very fast.

    This is on par with the performance of `DataFrameSetAccess`, but still
    tens of nanoseconds slower.
    """

    def __init__(self, table: pd.DataFrame, cols: Optional[list[str]] = None):
        """This performs the expensive caching operation, that must occur once.

        Args:
            table: The DataFrame to index into the Trie.
            cols: The specific columns of the DataFrame to index into the Trie.
                If None, all columns are used.
        """

        if not cols:
            cols = list(table.columns)
        self.cols = cols

        self.counts_dict: Counter[tuple[Any, ...]] = Counter()

        for row in np.array(table[cols]):
            # Update all partial trie nodes. For example, take row (val1, val2):
            # We would increment all tuples ()++, (val1)++, (val1, val2)++, ...
            for i in range(len(cols) + 1):
                # We include the empty tuple (row[0:0]) count also.
                t = tuple(row)[0:i]
                self.counts_dict[t] += 1

    def isin(self, values: tuple[tuple[Any, ...]]) -> bool:
        """A tuple will only be in the cache if the count is at least 1."""
        # This must remain very fast, so do not add additional asserts/check.
        return values in self.counts_dict

    def counts(self, values: tuple[tuple[Any, ...]]) -> int:
        """Get the number of rows that start with `values` tuple."""
        # This must remain very fast, so do not add additional asserts/check.
        return self.counts_dict[values]


def boot_sample(
    # This is the fastest input to rng.choice, other than a scalar.
    a: npt.NDArray[Any],
    *,
    n: Optional[int] = None,
    rng: np.random.Generator = np.random.default_rng(),
) -> npt.NDArray:
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
    rng: np.random.Generator = np.random.default_rng(),
) -> npt.NDArray[np.int64]:
    """Sample with replacement `range` elements from `0` to `range`.

    This is slightly faster than `fpsutils.boot_sample`.

    Equivalent to `rng.choice(range, size=range, replace=True)`.
    """

    return rng.choice(range, size=range, replace=True)


def plot_pd_column_hist_discrete(
    tbl: pd.DataFrame, column: str, title_prefix: Optional[str] = None
):
    """Plot the histogram of a single column of a DataFrame"""

    vals = np.unique(tbl[column], return_counts=True)
    plt.bar(*vals)
    plt.xticks(vals[0], rotation="vertical", fontsize=5)
    plt.xlabel(column)
    plt.ylabel("Count")
    if title_prefix:
        plt.title(f"{title_prefix} by {column}")


def plot_pd_hist_discrete(
    tbl: pd.DataFrame,
    title_prefix: Optional[str] = None,
    figsize: Optional[tuple] = None,
):
    """Plot the histograms of each column in a DataFrame.

    This is different than `pd.DataFrame.hist`, because it ensures that all
    unique elements of the column are represented in a bar plot. Other
    implementations will try to bin multiple values and doesn't center the
    graphical bars on the values.
    """

    num_plots = len(tbl.columns)
    if not figsize:
        figsize = (10, 6 * num_plots)

    plt.figure(figsize=figsize)
    for index, col in enumerate(tbl.columns):
        plt.subplot(num_plots, 1, index + 1)
        plot_pd_column_hist_discrete(tbl, col, title_prefix)
    plt.show()


def discrete_hist(data) -> tuple[npt.NDArray, npt.NDArray]:
    """Return a tuple of unique items and their counts.

    Returns:
        ([items], [counts])
    """

    return np.unique(data, return_counts=True)


def has_columns(df: pd.DataFrame, cols: Iterable[Union[Enum, str]]) -> bool:
    """Check if the DataFrame `df` contains all `cols`.

    This allows for specifying a list of Enums, whose `value` is the column
    name that is expected.
    """

    col_strings = {isinstance(c, Enum) and c.value or str(c) for c in cols}
    return col_strings <= {c for c in df.columns}


def plt_discrete_hist(data):
    counts = np.bincount(data)

    # We need to zoom in, since there would be thousands of thousands of bars that
    # are zero near the tail end.
    nonzero_indicies = np.nonzero(counts)
    first_index = np.min(nonzero_indicies)
    # first_index = 0
    last_index = np.max(nonzero_indicies)

    if (last_index - first_index) < 2000:
        # plt.title(f'Samples {np.size(r)} | p = {p:.3e} | Groups {groups}')

        x = np.arange(start=first_index, stop=last_index + 1)
        h = counts[first_index : last_index + 1]
        plt.bar(x, h)
        # plt.xticks(x)
        # plt.xlabel('Group Sums')
        plt.ylabel("Frequency")
        # plt.vlines(bin_edges[:np.size(bin_edges)-1], 0, hist)

        # Overlay Norm Curve
        mu, std = norm.fit(data)
        mean = np.mean(data)
        print(f"first={first_index} last={last_index}")
        print(
            f"mu={mu} , std={std} 3*std={3*std}, np.mean(data) = {np.mean(data)}"
        )

        # x_curve = x
        x_curve = np.linspace(mean - 3 * std, mean + 3 * std, 50)
        p = norm.pdf(x_curve, mu, std)
        p_scaled = p * np.sum(h)
        plt.plot(x_curve, p_scaled, "k", linewidth=2)
        plt.xticks([mean - 3 * std, mean, mean + 3 * std])
    else:
        display(
            Markdown(
                f"Plot is too large (first={first_index} last={last_index}),"
                " not diplaying."
            )
        )


def plt_discrete_hist2(data):
    """Plot a histogram of `data`, where all values are represented on x-axis.

    It also fit a normal curve and places vertical lines for the mean
    and 2x standard deviation limits.
    """
    vals, counts = np.unique(data, return_counts=True)
    plt.bar(vals, counts)
    plt.xticks(vals, rotation="vertical", fontsize=5)
    # plt.xlabel(col)
    plt.ylabel("Frequency")
    # plt.show()

    # Overlay normal curve that spans 3x standard deviations.
    mean, std = norm.fit(data)
    x_curve = np.linspace(mean - 3 * std, mean + 3 * std, 50)
    p = norm.pdf(x_curve, mean, std)
    p_scaled = p * np.sum(counts)
    plt.plot(x_curve, p_scaled, "k", linewidth=2)

    # Place 2x standard deviation confidence lines and mean.
    mid_y = np.max(counts) / 2
    for ci_x, ci_label in [
        (mean - 2 * std, "lower 2*std"),
        (mean, "mean"),
        (mean + 2 * std, "upper 2*std"),
    ]:
        plt.axvline(ci_x, color="red")
        plt.text(
            ci_x + 1,
            mid_y,
            f"{ci_x:.3f} is {ci_label}",
            rotation=90,
            color="red",
        )


def elapsed_time_str(sec: float) -> str:
    """Convert a seconds value into a more easily interpretable units str.

    Example: elapsed_time_str(0.003) -> "3ms"
    """

    # TODO: See if numpy.timedelta64 can be used.

    hour = int(sec / 60.0**2)
    sec -= hour * 60**2
    min = int(sec / 60.0)
    sec -= min * 60.0
    s = int(sec)
    ms = int(sec * 1e3) % 1000
    us = int(sec * 1e6) % 1000
    ns = (sec * 1e9) % 1000

    string = ""
    string += hour and f"{hour}hr " or ""
    string += min and f"{min}min " or ""
    string += s and f"{s}s " or ""
    string += ms and f"{ms}ms " or ""
    string += us and f"{us}us " or ""
    string += ns and f"{ns:3.3f}ns " or ""
    return string.rstrip()


def benchmark(
    stmt: str,
    setup: str = "pass",
    globals: dict[str, Any] = {**locals(), **globals()},
) -> tuple[int, float, float]:
    """Measure the runtime of `stmt`.

    This method invokes timeit.Timer.autorange and print results.

    Returns:
        (num_loops, sec_total, sec_per_loop)
    """

    loops, sec = timeit.Timer(stmt, setup=setup, globals=globals).autorange()
    print(
        f'Ran "{stmt}" {loops} times over {sec}s.'
        " "
        f"It took {elapsed_time_str(sec/loops)} per loop."
    )
    return loops, sec, np.divide(sec, loops)


def fmt_far(
    far_value: float, fmt: Literal["k", "s"] = "k", decimal_places: int = 3
) -> str:
    """Pretty print an FAR value.

    Args:
        `far_value` is the FAR value to show.

        `fmt` indicates the output format.
        Either `'k'` for `1/<FAR>k` or `'s'` for scientific notation.

        `decimal_places` indicates the number of values to show past
        the decimal point.
    """

    if not fmt in ["k", "s"]:
        raise TypeError("type must be 'k' or 's'.")

    if fmt == "k":
        if far_value == 0.0:
            return "0"
        return f"1/{{:.{decimal_places}f}}k".format(1 / (far_value * 1000))
    else:
        return f"{{:.{decimal_places}e}}".format(far_value)


def fmt_frr(frr_value: float, decimal_places: int = 3) -> str:
    """Pretty print an FRR value as a percentage.

    Args:
        `frr_value` is the FRR value to show.

        `decimal_places` indicates the number of values to show past
        the decimal point.
    """
    return f"{{:.{decimal_places}%}}".format(frr_value)
