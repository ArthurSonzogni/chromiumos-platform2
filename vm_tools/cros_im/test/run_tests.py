#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
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
import functools
import os
import signal
import subprocess
import sys
import time
from typing import Callable, Dict, List, Optional


TEST_BINARY_GTK3 = "./cros_im_tests_gtk3"
TEST_BINARY_GTK4 = "./cros_im_tests_gtk4"


def run_tests_with_wayland_server(test_func: Callable) -> bool:
    TEST_WAYLAND_SOCKET = "wl-cros-im-test"
    TEST_XDG_RUNTIME_DIR = os.getcwd()

    env_override = {
        "XDG_RUNTIME_DIR": f"{TEST_XDG_RUNTIME_DIR}",
        "WAYLAND_DISPLAY": f"{TEST_WAYLAND_SOCKET}",
    }
    env = os.environ.copy()
    env.update(env_override)

    with subprocess.Popen(
        [
            "xvfb-run",
            "-a",
            "weston",
            f"-S{TEST_WAYLAND_SOCKET}",
            "-Bx11-backend.so",
        ],
        env=env,
        start_new_session=True,
    ) as proc:
        try:
            for i in range(10):
                print(f"Waiting up to 10s for Weston to start... ({i+1}/10s)")
                time.sleep(1)
                if os.path.exists(
                    os.path.join(TEST_XDG_RUNTIME_DIR, TEST_WAYLAND_SOCKET)
                ):
                    break
            else:
                print("Failed to start Weston.")
                return False

            print("Weston started, running tests...")
            return test_func(xvfb_env_override=env_override)
        finally:
            wl_server_pgid = os.getpgid(proc.pid)
            os.killpg(wl_server_pgid, signal.SIGTERM)


def verify_in_build_directory() -> bool:
    if not os.path.isfile(TEST_BINARY_GTK3):
        print(
            f"Could not find {TEST_BINARY_GTK3}. "
            "This script should be run from a cros_im build directory."
        )
        return False
    return True


def get_gnu_type() -> str:
    get_gnu_process = subprocess.run(
        ["dpkg-architecture", "-q", "DEB_BUILD_MULTIARCH"],
        capture_output=True,
        check=True,
        text=True,
    )
    return get_gnu_process.stdout.strip()


def set_up_gtk3_immodules_cache() -> bool:
    with open("test_immodules.cache", "w", encoding="utf-8") as f:
        try:
            gnu_type = get_gnu_type()
            subprocess.call(
                [
                    f"/usr/lib/{gnu_type}/libgtk-3-0/gtk-query-immodules-3.0",
                    "libim-test-cros-gtk3.so",
                ],
                stdout=f,
            )
            return True
        except subprocess.CalledProcessError as e:
            print(e.output.decode())
            print(e)
            return False


def set_up_gtk4_immodule() -> bool:
    try:
        gnu_type = get_gnu_type()
        subprocess.run(
            [
                "sudo",
                "cp",
                "libim-test-cros-gtk4.so",
                f"/usr/lib/{gnu_type}/gtk-4.0/4.0.0/immodules/.",
            ],
            capture_output=True,
            check=True,
            text=True,
        )
        return True
    except subprocess.CalledProcessError as e:
        print(e.output.decode())
        print(e)
        return False


def get_test_names(
    test_binary: Optional[str], test_filter: Optional[str]
) -> List[str]:
    args = [test_binary, "--gtest_list_tests"]
    if test_filter is not None:
        args.append(f"--gtest_filter={test_filter}")
    stdout = subprocess.check_output(args)
    lines = stdout.decode().strip().split("\n")
    result = []
    assert lines[0].startswith("Running main() from ")
    # The output of --gtest_list_tests is formatted like:
    # GroupName1.
    #     TestName1
    #     TestName2
    for line in lines[1:]:
        if line.endswith("."):
            group = line
        else:
            assert line.startswith("  ")
            result.append(group + line.strip())

    return result


def run_gtk_wayland_tests(
    test_filter: Optional[str],
    test_binary: Optional[str],
    xvfb_env_override: Optional[Dict[str, str]] = None,
) -> bool:
    env_override = {
        "CROS_IM_VIRTUAL_KEYBOARD": "1",
        "GTK_IM_MODULE_FILE": "test_immodules.cache",
        "GTK_IM_MODULE": "test-cros",
        "GDK_BACKEND": "wayland",
    }

    if xvfb_env_override:
        env_override.update(xvfb_env_override)

    env_override_str = " ".join(f"{k}={v}" for k, v in env_override.items())
    env = os.environ.copy()
    env.update(env_override)

    timeout_s = 10

    successes = []
    failures = []
    for test in get_test_names(test_binary, test_filter):
        args = [test_binary, f"--gtest_filter={test}"]
        print("=" * 80)
        print(f'Running: {env_override_str} {" ".join(args)}')
        try:
            completed_process = subprocess.run(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=timeout_s,
                check=True,
                env=env,
            )
            output = completed_process.stdout.decode()
            print(output)
            success = "BACKEND ERROR: " not in output
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
            print(e.output.decode())
            print(e)
            success = False

        if success:
            successes.append(test)
        else:
            failures.append(test)

    print("=" * 80)
    if successes:
        print("Passed:")
        for test in successes:
            print(f"- {test}")
    if failures:
        print("Failed:")
        for test in failures:
            print(f"- {test}")
        return False

    return True


def main() -> None:
    if not verify_in_build_directory():
        return

    parser = argparse.ArgumentParser()
    parser.add_argument("--gtest_filter", help="Restrict test cases run")
    parser.add_argument(
        "--suite",
        choices=["gtk3", "gtk4"],
        help="Test suite to run",
        required=True,
    )
    parser.add_argument(
        "--with_xvfb", action="store_true", help="Run tests on xvfb"
    )
    args = parser.parse_args()

    if args.suite == "gtk4" and not set_up_gtk4_immodule():
        sys.exit("Failed to setup test IM module for GTK4.")
    if args.suite == "gtk3" and not set_up_gtk3_immodules_cache():
        sys.exit("Failed to set up immodules for GTK3.")

    tests_passed = True

    test_binary = TEST_BINARY_GTK4 if args.suite == "gtk4" else TEST_BINARY_GTK3

    gtk_wayland_runner = functools.partial(
        run_gtk_wayland_tests, args.gtest_filter, test_binary
    )

    if args.with_xvfb:
        tests_passed = tests_passed and run_tests_with_wayland_server(
            gtk_wayland_runner
        )
    else:
        tests_passed = tests_passed and gtk_wayland_runner()

    if not tests_passed:
        sys.exit("At least one test did not pass.")


if __name__ == "__main__":
    main()
