#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import multiprocessing as mp
import time
from typing import Any, Callable, Iterable, List, Optional

from experiment import Experiment
import fpsutils
import numpy as np


# There are many issues and performance bottlenecks when using
# multiprocessing. Checkout some of the articles on
# https://thelaziestprogrammer.com/python/a-multiprocessing-pool-pickle .


class Bootstrap:
    """The base class for implementing a bootstrap sample over an Experiment."""

    USE_GLOBAL_SHARING = False
    """Enables the use of sharing process data by global inheritance.

    Enable this when the serialization of the `self` data has become too much
    of a parallelization bottleneck. You can see this IO overhead when running
    the sample. Looking at the CPU monitor, if CPU loads are not being fully
    utilized, this could mean that they are blocked on IO. Try enabling this
    flag.
    """

    def __init__(self, exp: Experiment, verbose: bool = False) -> None:
        self._verbose = verbose
        if self._verbose:
            print("# Initializing Runtime and Caches.")
        self._time_init_start = time.time()
        self._init(exp)
        self._time_init_end = time.time()

        self._time_rng_start = 0.0
        self._time_rng_end = 0.0
        self._time_pool_startup_start = 0.0
        self._time_pool_startup_end = 0.0
        self._time_samples_start = 0.0
        self._time_samples_end = 0.0

    def _init(self, exp: Experiment) -> None:
        """Initialize runtime caches.

        Implement this.

        This will be run once before any `_sample` invocations occur.
        Save the absolute least amount of data in `self` as possible, since
        this data must be copied to each worker process in certain runtime
        configurations.
        """
        return

    def _sample(self, rng: np.random.Generator) -> int:
        """Complete a single bootstrap sample using.

        Implement this.

        Args:
            rng: The initialized random number generator to use for the sample.

        Return:
            Returns that count of values being observed in the sample.
        """
        return 0

    @classmethod
    def _process_global_init(cls, data):
        cls.__rng = np.random.default_rng()
        cls.__data: Bootstrap = data

    @classmethod
    def _process_global_sample(cls, sample_id: int) -> int:
        return cls.__data._sample(cls.__rng)

    def __run_single_process(
        self,
        num_samples: int,
        progress: Callable[[Iterable[Any], int], Iterable[Any]],
    ) -> List[int]:
        boot_counts = list()

        if self._verbose:
            print("# Initializing RNG.")
        self._time_rng_start = time.time()
        rng = np.random.default_rng()
        self._time_rng_end = time.time()

        if self._verbose:
            print("# Starting Bootstrap Samples.")
        self._time_samples_start = time.time()
        for _ in progress(range(num_samples), num_samples):
            boot_counts.append(self._sample(rng))
        self._time_samples_end = time.time()

        return boot_counts

    def __run_multi_process(
        self,
        num_samples: int,
        num_proc: Optional[int],
        progress: Callable[[Iterable[Any], int], Iterable[Any]],
    ) -> List[int]:
        # All processes must have their own unique seed for their pseudo random
        # number generator, otherwise they will all generate the same random values.
        #
        # For reproducibility and testing, you can use np.random.SeedSequence.spawn
        # or [np.random.default_rng(i) for i in range(BOOTSTRAP_SAMPLES)]
        # See https://numpy.org/doc/stable/reference/random/parallel.html .
        #
        # We could possible generate fewer random number generators, since we only
        # need one per process. This might be achievable by using the Pool
        # constructor's initializer function to add a process local global
        # rng variable. For now, it is just safer to generate BOOTSTRAP_SAMPLES
        # random number generators. Do note that you can't really parallelize
        # the rng initialization, since the CPU doesn't produce enough entropy
        # to satisfy threads concurrently. They end up just being blocked
        # on IO.

        if num_proc == 0:
            num_proc = mp.cpu_count()

        if self._verbose:
            print(
                f"# Dispatching {num_samples} bootstrap samples over "
                f"{num_proc} processes."
            )

        if self.USE_GLOBAL_SHARING:
            # It is my understanding, from experimentation and documentation,
            # that any normal object passed into the pool.map call will be
            # serialized and sent to the worker processes. If the objects being
            # sent are large, this serialization/deserialization can take some
            # time and can be error prone if there are circular references.
            #
            # The worker processes will inherit [without serialization] global
            # and local context for executing the `initializer` with `initargs`.
            # We maintain the global context, but loose local references when
            # executing subsequent pool executions operations.
            #
            # There are probably other performant mechanisms for sharing data
            # efficiently on the same machine, but one of the fastest mechanisms
            # I have experienced is exploiting the process initializer's lack of
            # serialization to save the local object reference in each
            # processes' global state.
            # Although all permutations of this mechanism are subtle hack,
            # using a classmethod to silo the global data to the class itself
            # adds a small amount of scoping. Furthermore, this isn't
            # necessarily a scope collision with other purposes, since the
            # global scope is only clobbered within each pool process, not
            # the parent process. the assumption is that you much reinitialize
            # a new pool for each Bootsrap sampling.
            self._time_pool_startup_start = time.time()
            with mp.Pool(
                initializer=self._process_global_init,
                initargs=(self,),
                processes=num_proc,
            ) as pool:
                self._time_pool_startup_end = time.time()

                if self._verbose:
                    print("# Blastoff.")
                self._time_samples_start = time.time()
                boot_counts = pool.map(
                    self._process_global_sample,
                    progress(range(num_samples), num_samples),
                )
                self._time_samples_end = time.time()

                return boot_counts
        else:
            self._time_pool_startup_start = time.time()
            with mp.Pool(processes=num_proc) as pool:
                self._time_pool_startup_end = time.time()

                if self._verbose:
                    print("# Initializing RNGs.")
                self._time_rng_start = time.time()
                rngs = progress(
                    (np.random.default_rng(i) for i in range(num_samples)),
                    num_samples,
                )
                self._time_rng_end = time.time()

                if self._verbose:
                    print("# Blastoff.")
                self._time_samples_start = time.time()
                boot_counts = pool.map(self._sample, rngs)
                self._time_samples_end = time.time()

                return boot_counts

    def run(
        self,
        num_samples: int = 5000,
        num_proc: Optional[int] = 0,
        progress: Callable[
            [Iterable[Any], int], Iterable[Any]
        ] = lambda it, total: iter(it),
    ) -> List[int]:
        """Run all bootstrap samples.

        Args:
            num_samples: Number of bootstrap samples should be acquired.
                         1000 samples for 95% CI
                         5000 samples for 99% CI
            num_proc: Number of simultaneous processes to use.
                      (None = Sequential | 0 = Max Processors | # = Specific #)
            progress: An interable function that can track the samples progress.
        """

        if num_proc is None:
            boot_counts = self.__run_single_process(num_samples, progress)
        else:
            boot_counts = self.__run_multi_process(
                num_samples, num_proc, progress
            )

        if self._verbose:
            self.print_timing()

        return boot_counts

    def print_timing(self):
        """Print the saved timing values."""

        delta_init = self._time_init_end - self._time_init_start
        delta_rng = self._time_rng_end - self._time_rng_start
        delta_pool_startup = (
            self._time_pool_startup_end - self._time_pool_startup_start
        )
        delta_samples = self._time_samples_end - self._time_samples_start

        total = delta_init + delta_rng + delta_pool_startup + delta_samples

        print(f"Cache init took {delta_init:.6f}s.")
        print(f"RNG init took {delta_rng:.6f}s.")
        print(
            f"Process pool setup took {delta_pool_startup:.6f}s"
            f'{self.USE_GLOBAL_SHARING and " (includes RNG init)" or ""}.'
        )
        print(f"Bootstrap sampling took {delta_samples:.6f}s.")
        print("-----------------------------------------")
        print(f"Total combined runtime was {total:.6f}s.")


class BootstrapFullFARHierarchy(Bootstrap):
    # For the 100000 sample case, enabling this reduces overall time by
    # 3 seconds.
    USE_GLOBAL_SHARING = True

    def _init(self, exp: Experiment) -> None:
        fa_table = exp.fa_table()
        # The accepted tuples for fa_set and fa_trie.
        fa_set_tuple = [
            Experiment.TableCol.Verify_User.value,
            Experiment.TableCol.Enroll_User.value,
            Experiment.TableCol.Verify_Finger.value,
            Experiment.TableCol.Enroll_Finger.value,
            Experiment.TableCol.Verify_Sample.value,
        ]
        assert fpsutils.has_columns(fa_table, fa_set_tuple)
        self.fa_set = fpsutils.DataFrameSetAccess(fa_table, fa_set_tuple)
        self.fa_trie = fpsutils.DataFrameCountTrieAccess(fa_table, fa_set_tuple)

        self.users_list = exp.user_list()
        self.fingers_list = exp.finger_list()
        self.samples_list = exp.sample_list()

        # If you add exp, fa_table, and far_decisions to the self object,
        # it becomes no longer runtime feasible to use USE_GLOBAL_SHARING=False.
        # For some reason, the total runtime grows with respect to process
        # count.

    def _sample(self, rng: np.random.Generator) -> int:
        """Complete a single bootstrap sample using full hierarchy bootstrap method.

        The important parts of this approach are the following:
        * Avoiding DataFrame queries during runtime. We use fa_set cache instead.
        This is the difference between hundreds of _us_ vs. hundreds of _ns_
        per query.
        * Using rng.choice (now boot_sample) on either a scalar, np.array, or list
        is important. Other methods are very slow.
        * The big finale that gets the single bootstrap sample time down from
        tens of _s_ to hundreds of _ms_ is checking for the loop abort path
        using the fa_trie.

        Additional Performance:
        * Use boot_sample_range instead os boot_sample.
        * Flatten loops.
        """

        user_list = self.users_list
        fingers_list = self.fingers_list
        samples_list = self.samples_list

        fa_set = self.fa_set
        fa_trie = self.fa_trie

        sample = []
        # 72 users
        for v in fpsutils.boot_sample(user_list, rng=rng):
            if not fa_trie.isin((v,)):
                continue
            # 72 other template users (same user with different finger is allowed)
            for t in fpsutils.boot_sample(user_list, rng=rng):
                if not fa_trie.isin((v, t)):
                    continue
                # 6 fingers
                for fv in fpsutils.boot_sample(fingers_list, rng=rng):
                    if not fa_trie.isin((v, t, fv)):
                        continue
                    for ft in fpsutils.boot_sample(fingers_list, rng=rng):
                        if not fa_trie.isin((v, t, fv, ft)):
                            continue
                        # 60 verification samples
                        for a in fpsutils.boot_sample(samples_list, rng=rng):
                            # We don't check for the invalid matching case,
                            # (v == t) and (fv == ft), since it is slower than
                            # simply querying for the results.
                            # We actually don't care about omitting these
                            # cases because it will always receive a 0 from the
                            # query.
                            # As long as we always use the sum operation to
                            # aggregate all subsample data, no problems should
                            # arise.
                            query = (v, t, fv, ft, a)
                            sample.append(fa_set.isin(query))
                            # pass
        return np.sum(sample)
