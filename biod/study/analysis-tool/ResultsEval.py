# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# To add a new cell, type '# %%'
# To add a new markdown cell, type '# %% [markdown]'
# %% # Imports

#! %pip install pandas numpy matplotlib scipy tqdm ipympl

# NOTE: Sometimes this autoreload/autoimport feature messes up the environment.
#       If you see odd issues, like assert statements failing for TestCases,
#       you need to restart your kernel. This is probably because it redefines
#       the enums, which makes previous save enum values not equal to the new
#       enum value.
#! %load_ext autoreload
#! %autoreload 1

import os
import pathlib
from typing import List, Optional

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import scipy.stats as st
from IPython.display import HTML, Markdown, display
from matplotlib import pyplot as plt
from scipy.stats import norm
# import tqdm
# import tqdm.notebook as tqdm
from tqdm.autonotebook import tqdm  # Auto detect notebook or console.

#! %aimport fpsutils, simulate_fpstudy, experiment, fpc_bet_results
import bootstrap
import fpsutils
import simulate_fpstudy
from experiment import Experiment
from fpc_bet_results import FPCBETResults

# https://ipython.readthedocs.io/en/stable/interactive/magics.html#magic-matplotlib
# To open matplotlib in interactive mode
# %matplotlib qt5
# %matplotlib notebook
# For VSCode Interactive
# get_ipython().run_line_magic('matplotlib', "widget")
#! %matplotlib widget
# %matplotlib gui
# For VSCode No Interactive
# %matplotlib inline
# %matplotlib --list


def print_far_value(val: float, k_comparisons: List[int] = [20, 50, 100, 500]) -> str:
    """Print the science notation """
    str = f'{val:.4e}'
    for c in k_comparisons:
        str += f' = {val * 100 * c*1000:.3f}% of 1/{c}k'
    return str


# %% Load Data
# USE_SIMULATED_DATA = True
USE_SIMULATED_DATA = False
# USE_SIMULATED_PROB = 1/100000.0
USE_SIMULATED_PROB = 1/155000.0
# USE_SIMULATED_PROB = 1/175000.0
# USE_SIMULATED_PROB = 1/200000.0

exp: Optional[Experiment] = None

if USE_SIMULATED_DATA:
    # Load simulated data
    print('# Loading Simulated Data')
    sim_far_decisions = simulate_fpstudy.GenerateFARResults(
        num_users=72,
        num_fingers=6,
        num_verify_samples=80,
        user_groups=['A', 'B', 'C', 'D', 'E', 'F'],
        prob=USE_SIMULATED_PROB,
        verbose=True,
    )
    exp = Experiment(num_verification=60,
                     num_fingers=6,
                     num_users=72,
                     far_decisions=sim_far_decisions)
else:
    # Find Data
    # We expect a symbolic link named `data` to point to the directory that
    # contains the FPC BET reports and raw collection.
    print('# Discover Root Data Directory')
    data_root = pathlib.Path(os.readlink('data'))
    if not data_root.is_dir():
        raise Exception('Data root doesn\'t exist')
    print(f'Using root {data_root}')

    # REPORT_DIR = str(data_root.joinpath('report2'))
    REPORT_DIR = data_root.joinpath('report-100k')
    COLLECTION_DIR = data_root.joinpath('orig-data')

    # %% Import Data From BET Results

    bet = FPCBETResults(REPORT_DIR)

    decisions_cases = FPCBETResults.TestCase.all()

    far_decisions = bet.read_files(list(zip(
        decisions_cases,
        [FPCBETResults.TableType.FAR_Decision]*len(decisions_cases)
    )))

    frr_decisions = bet.read_files(list(zip(
        decisions_cases,
        [FPCBETResults.TableType.FRR_Decision]*len(decisions_cases)
    )))

    exp = Experiment(num_verification=60,
                     num_fingers=6,
                     num_users=72,
                     far_decisions=far_decisions[0],
                     )


exp.add_groups_from_collection_dir(COLLECTION_DIR)
# exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'A']
# exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'B']
# exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'C']
# exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'D']
# exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'E']

# %% Check

display(Markdown('''
# Distribution of False Accepts

This shows the number of false accepts as a function of each parameter.
This helps to show if there is some enrollment user, verification user, finger,
or sample that has an unusually high false acceptable rate.
'''))
fpsutils.plot_pd_hist_discrete(exp.fa_table(), title_prefix='False Accepts')

# %%

# 1000 samples is 95%
# 5000 samples is 99%
# BOOTSTRAP_SAMPLES = 1000
# BOOTSTRAP_SAMPLES = 5000  # 5000 samples in 2 seconds (128 cores)
BOOTSTRAP_SAMPLES = 100000  # 100000 samples in 16 seconds (128 cores)
CONFIDENCE_PERCENT = 95
PARALLEL_BOOTSTRAP = True

# %% Run FAR bootstrap

boot = bootstrap.BootstrapFullFARHierarchy(exp, verbose=True)
boot_means = boot.run(num_samples=BOOTSTRAP_SAMPLES,
                      num_proc=0,
                      progress=lambda it, total: tqdm(it, total=total))

# %%

df = pd.DataFrame({'Sample Means': boot_means}, dtype=int)
print(df)
print(df.describe())

# %%

plt.figure()
plt.title('Histogram of all unique bootstrap samples (bin=1)')
# fpsutils.plt_discrete_hist(boot_means)
fpsutils.plt_discrete_hist2(boot_means)
plt.show()

print(f'Total Cross Matches: {exp.fa_trials_count()}')
print(f'Total Cross False Accepts: {exp.fa_count()}')
boot_mean = np.mean(boot_means)  # bootstrapped sample means
print('Mean:', boot_mean)
boot_std = np.std(boot_means)  # bootstrapped std
print('Std:', boot_std)

ci_percent_lower = (100-CONFIDENCE_PERCENT) / 2
ci_percent_upper = 100-ci_percent_lower
# 95% C.I.
boot_limits = np.percentile(boot_means, [ci_percent_lower, ci_percent_upper])
print('Limits:', boot_limits)
limit_diff = boot_limits[1] - boot_limits[0]
print('Limits Diff:', limit_diff)


#################

boot_means_ratio = np.divide(boot_means, exp.fa_trials_count())

# far_limits = np.array([
#     boot_limits[0] / float(exp.FARMatches()),
#     boot_limits[1] / float(exp.FARMatches()),
# ])
far_limits = np.divide(boot_limits, exp.fa_trials_count())
far_mean = np.divide(boot_mean, exp.fa_trials_count())
far_std = np.divide(boot_std, exp.fa_trials_count())

plt.figure()
# fpsutils.plt_discrete_hist(np.divide(boot_means, exp.FARMatches()))

# Fit a normal distribution to the data:
# mean and standard deviation
mu, std = norm.fit(boot_means_ratio)
# Plot the histogram.
bins = 25
# bins=50
plt.hist(boot_means_ratio, bins=bins, density=True, alpha=0.6, color='b')

# Plot the PDF.
xmin, xmax = plt.xlim()
x = np.linspace(xmin, xmax, 100)
p = norm.pdf(x, mu, std)

plt.plot(x, p, 'k', linewidth=2)
plt.title(f'Fit Values: {mu} and {std}')

plt.axvline(x=far_limits[0], color='blue')
plt.axvline(x=far_limits[1], color='blue')
plt.axvline(x=1/100000.0, color='red')
plt.text(1/100000.0, 100000, '1/100k', rotation=90, color='red')
plt.axvline(x=1/200000.0, color='red')
plt.text(1/200000.0, 100000, '1/200k', rotation=90, color='red')

plt.show()

print('FAR Mean:', far_mean)
print('FAR Std:', far_std)

print(f'FAR Limits: {far_limits} <= 1/100k = {far_limits <= 1/100000.0}')
print('FAR Limits:', [print_far_value(far_limits[0], [100]),
                      print_far_value(far_limits[1], [100])])
print(f'FAR Limits 1/{1/far_limits}')
print(f'FAR Limits {far_limits <= 1/100000.0}')


###############################################################################

# %%
#! %%time

# This is the more advanced naive approach to demonstrate what we are doing.
# This tries to use the faster boot_sample_range, but this requires setting up
# user id maps.

# %% Attempt to flatten the loops.
#! %time

# # 72 users
# sample_verify_users = rng.choice(exp.num_users,
#                                     size=exp.num_users,
#                                     replace=True)
# sample_verify_users = np.repeat(sample_verify_users, exp.num_users-1)
# # against 71 other template users
# sample_enroll_users = rng.choice(exp.num_users-1,
#                                     size=exp.num_users*(exp.num_users-1),
#                                     replace=True)
# sample_users = np.stack(
#     (sample_verify_users, sample_enroll_users), axis=1)

# # Enforce nested loop invariant:
# # Effectively "omit" the verify user id from the enroll users
# sample_users[sample_users[:, 0] <= sample_users[:, 1]][:, 1] += 1
