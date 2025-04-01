# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Configuration and fixtures for pytest.

See the following doc link for an explanation of conftest.py and how it is used
by pytest:
https://docs.pytest.org/en/latest/explanation/fixtures.html
"""


def pytest_addoption(parser):
    """Adds additional options to the default pytest CLI args."""
    parser.addoption(
        "--no-chroot",
        dest="chroot",
        action="store_false",
        help="Skip any tests that require a chroot to function.",
    )
