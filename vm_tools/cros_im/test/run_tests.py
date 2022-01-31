#!/usr/bin/env python3
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Run cros_im tests, with functionality hard to add in the gtest runner.

This script runs cros_im tests, setting up environment variables and checking
for errors from the backend. Tests are run separately as running multiple tests
directly from the test app currently does not work as global GTK state can
not be reset, so the IM module can not be re-initialized.

This script should be invoked from the build directory, e.g.
    cros_im/build$ ninja && ../test/run_tests.py
    cros_im/build$ ninja && ../test/run_tests.py --gtest_filter=*KeySym*
"""

import argparse
import os
import subprocess
from typing import List, Optional

TEST_BINARY = './cros_im_tests'

def verify_in_build_directory() -> bool:
    if not os.path.isfile(TEST_BINARY):
        print(f'Could not find {TEST_BINARY}. '
              'This script should be run from a cros_im build directory.')
        return False
    return True

def set_up_immodules_cache() -> None:
    with open('test_immodules.cache', 'w') as f:
        subprocess.call([
            '/usr/lib/x86_64-linux-gnu/libgtk-3-0/gtk-query-immodules-3.0',
            'libim_test_cros_gtk.so'
            ], stdout=f)

def get_test_names(test_filter: Optional[str]) -> List[str]:
    args = [TEST_BINARY, '--gtest_list_tests']
    if test_filter is not None:
        args.append(f'--gtest_filter={test_filter}')
    stdout = subprocess.check_output(args)
    lines = stdout.decode().strip().split('\n')
    result = []
    assert lines[0].startswith('Running main() from ')
    # The output of --gtest_list_tests is formatted like:
    # GroupName1.
    #     TestName1
    #     TestName2
    for line in lines[1:]:
        if line.endswith('.'):
            group = line
        else:
            assert line.startswith('  ')
            result.append(group + line.strip())

    return result

def run_gtk3_wayland_tests(test_filter: Optional[str]) -> None:
    env_override = {
        'GTK_IM_MODULE_FILE': 'test_immodules.cache',
        'GTK_IM_MODULE': 'test-cros',
        'GDK_BACKEND': 'wayland',
    }
    env_override_str = ' '.join(f'{k}={v}' for k, v in env_override.items())
    env = os.environ.copy()
    env.update(env_override)

    timeout_s = 2

    successes = []
    failures = []
    for test in get_test_names(test_filter):
        args = [TEST_BINARY, f'--gtest_filter={test}']
        print('='*80)
        print(f'Running: {env_override_str} {" ".join(args)}')
        try:
            completed_process = subprocess.run(args, stdout=subprocess.PIPE,
                                               stderr=subprocess.STDOUT,
                                               timeout=timeout_s, check=True,
                                               env=env)
            output = completed_process.stdout.decode()
            print(output)
            success = 'BACKEND ERROR: ' not in output
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
            print(e.output.decode())
            print(e)
            success = False

        if success:
            successes.append(test)
        else:
            failures.append(test)

    print('='*80)
    if successes:
        print('Passed:')
        for test in successes:
            print(f'- {test}')
    if failures:
        print('Failed:')
        for test in failures:
            print(f'- {test}')

def main() -> None:
    if not verify_in_build_directory():
        return

    parser = argparse.ArgumentParser()
    parser.add_argument('--gtest_filter', help='Restrict test cases run')
    args = parser.parse_args()

    set_up_immodules_cache()
    run_gtk3_wayland_tests(args.gtest_filter)

if __name__ == '__main__':
    main()
