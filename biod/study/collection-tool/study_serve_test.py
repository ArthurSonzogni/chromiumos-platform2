#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The unit test suite for the Fingerprint Study Tool."""

from __future__ import print_function

import os
import unittest
import unittest.mock

import study_serve


class TestStudyServeEnvironmentVariableParsing(unittest.TestCase):
    """Test the |environment_parameters| function."""

    PARAMS = {
        "first": "first-value",
        "second": True,
        "third": 1,
        "fourth": "",
        "fifth": 1.78,
    }

    PARAMS_NEW = {
        "first": "new-value",
        "second": False,
        "third": 98,
        "fourth": "new-value",
        "fifth": 2.0,
    }

    PARAMS_ENV = {
        "FIRST": str(PARAMS_NEW["first"]),
        "SECOND": str(PARAMS_NEW["second"]),
        "THIRD": str(PARAMS_NEW["third"]),
        "FOURTH": str(PARAMS_NEW["fourth"]),
        "FIFTH": str(PARAMS_NEW["fifth"]),
    }

    def test_environment_parameters_no_touch(self):
        with unittest.mock.patch.dict(os.environ, {}, clear=True):
            env = study_serve.environment_parameters(self.PARAMS.copy())

        for param, value in self.PARAMS.items():
            self.assertEqual(
                env[param],
                value,
                f'Param "{param}" was ' f'overwritten with "{env[param]}".',
            )

    def test_environment_parameters_name_translation(self):
        default_params = {
            "log-dir": "default",
        }
        with unittest.mock.patch.dict(
            os.environ, {"LOG_DIR": "new-value"}, clear=True
        ):
            env = study_serve.environment_parameters(default_params)

        self.assertEqual(env["log-dir"], "new-value")

    def test_environment_parameters_types(self):
        with unittest.mock.patch.dict(os.environ, self.PARAMS_ENV, clear=True):
            env = study_serve.environment_parameters(self.PARAMS.copy())

        for param, value in self.PARAMS_NEW.items():
            self.assertEqual(env[param], value, f'Param "{param}" was invalid.')

    def test_environment_parameters_types_error(self):
        default_params = {
            "port": 9000,
        }
        environ = {
            "PORT": "not-a-number",
        }
        with unittest.mock.patch.dict(os.environ, environ, clear=True):
            with self.assertRaisesRegex(ValueError, "PORT"):
                _ = study_serve.environment_parameters(default_params)

    def test_environment_parameters_bools(self):
        """Test the parsing of bools from environment variable."""

        # Parse "True" from env.
        with unittest.mock.patch.dict(
            os.environ, {"SYSLOG": "True"}, clear=True
        ):
            env = study_serve.environment_parameters({"syslog": False})
        self.assertEqual(env["syslog"], True)

        # Parse "False" from env.
        with unittest.mock.patch.dict(
            os.environ, {"SYSLOG": "False"}, clear=True
        ):
            env = study_serve.environment_parameters({"syslog": True})
        self.assertEqual(env["syslog"], False)

        # Parse "yes" from env.
        with unittest.mock.patch.dict(
            os.environ, {"SYSLOG": "yes"}, clear=True
        ):
            env = study_serve.environment_parameters({"syslog": False})
        self.assertEqual(env["syslog"], True)

        # Parse "no" from env.
        with unittest.mock.patch.dict(os.environ, {"SYSLOG": "no"}, clear=True):
            env = study_serve.environment_parameters({"syslog": True})
        self.assertEqual(env["syslog"], False)


if __name__ == "__main__":
    unittest.main()
