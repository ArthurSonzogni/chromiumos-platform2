# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The setuptools setup file."""

from __future__ import print_function

from setuptools import setup

setup(
    name='cros_config_host',
    version='1',
    author='Simon Glass',
    author_email='sjg@chromium.org',
    url='README.md',
    packages=['cros_config_host'],
    package_data={'cros_config_host': ['cros_config_schema.yaml']},
    entry_points={
        'console_scripts': [
            'cros_config_host = cros_config_host.cros_config_host:main',
            'cros_config_schema = cros_config_host.cros_config_schema:main',
            'validate_config = cros_config_host.validate_config:Main',
        ],
    },
    description='Access to the master configuration from the host',
)
