#!/usr/bin/env python3
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for platform2.py"""

import os
from unittest import mock

import platform2


_HACK_VAR_TO_DISABLE_ISORT = "hack"

# pylint: disable=wrong-import-position
import chromite_init  # pylint: disable=unused-import

from chromite.lib import cros_test_lib


PLATFORM_SUBDIR = "platform"
SYSROOT = "/"
TARGET_PREFIX = "//%s:" % PLATFORM_SUBDIR


class Platform2Configure(cros_test_lib.TestCase):
    """A base class of Platform2 unittest."""

    @staticmethod
    def _CreateTestPlatform2():
        p2 = platform2.Platform2()
        p2.platform_subdir = PLATFORM_SUBDIR
        p2.sysroot = SYSROOT
        return p2

    def _RunWithDesc(self, func, gn_description):
        """Runs Platform2.|func| with fake |gn_description|."""
        p2 = self._CreateTestPlatform2()
        with mock.patch(
            "platform2.Platform2.gn_desc", return_value=gn_description
        ):
            return getattr(p2, func)()


class Platform2ConfigureTest(Platform2Configure):
    """Tests Platform2.configure_test()."""

    @staticmethod
    def _CreateTestData(run_test=True, test_config=None):
        """Generates a template of test data."""

        # It emulates a data that is generated by the templates in
        # //common-mk/BUILDCONFIG.gn for this BUILD.gn rule example
        #
        # group("all") {
        #   deps = ["//platform:test"]
        # }
        #
        # executable("test") {
        #   output_name = "output"
        #
        #   run_test = $run_test
        #   test_config = $test_config
        #
        #   # some required variables
        # }
        if test_config is None:
            test_config = {}
        return {
            TARGET_PREFIX
            + "all": {
                "deps": [
                    TARGET_PREFIX + "test",
                ],
            },
            TARGET_PREFIX
            + "test": {
                "metadata": {
                    "_run_test": [run_test],
                    "_test_config": [test_config],
                },
                "outputs": ["output"],
            },
        }

    def _CheckConfigureTest(self, gn_description, expected):
        """Helper function to verify output of configure_test."""
        ret = self._RunWithDesc("configure_test", gn_description)
        self.assertEqual(ret, expected)

    @staticmethod
    def _OutputTemplate(options):
        """Create Output Template.

        Add platform2_test.py and some required options to the beginning.
        """
        platform_tooldir = os.path.dirname(os.path.abspath(__file__))
        p2_test_py = os.path.join(platform_tooldir, "platform2_test.py")
        prefix = [p2_test_py, "--action=run", "--sysroot=%s" % SYSROOT]
        return prefix + options

    def testMultipleTest(self):
        """Verify it can execute multiple tests."""
        targets = [TARGET_PREFIX + "test%s" % i for i in range(10)]
        desc_data = {
            TARGET_PREFIX
            + "all": {
                "deps": targets,
            },
        }
        for target in targets:
            desc_data[target] = {
                "metadata": {
                    "_run_test": [True],
                },
                "outputs": [
                    "test-%s" % target,
                ],
            }
        self._CheckConfigureTest(
            desc_data,
            [
                self._OutputTemplate(["--", "test-%s" % target])
                for target in targets
            ],
        )

    def testRunTest(self):
        """Verify it executes test only when run_test is true."""
        self._CheckConfigureTest(
            self._CreateTestData(run_test=True),
            [self._OutputTemplate(["--", "output"])],
        )
        self._CheckConfigureTest(self._CreateTestData(run_test=False), [])

    def testBooleanConfigs(self):
        """Verify it converts boolean configs to flag options."""
        self._CheckConfigureTest(
            self._CreateTestData(
                test_config={
                    "run_as_root": True,
                }
            ),
            [self._OutputTemplate(["--run_as_root", "--", "output"])],
        )
        self._CheckConfigureTest(
            self._CreateTestData(
                test_config={
                    "run_as_root": False,
                }
            ),
            [self._OutputTemplate(["--", "output"])],
        )

    def testStringConfigs(self):
        """Verify it converts string configs to not-flag options."""
        self._CheckConfigureTest(
            self._CreateTestData(
                test_config={
                    "gtest_filter": "-*.RunAsRoot",
                }
            ),
            [
                self._OutputTemplate(
                    ["--gtest_filter=-*.RunAsRoot", "--", "output"]
                )
            ],
        )


class Platform2ConfigureInstall(Platform2Configure):
    """Tests Platform2.configure_install()."""

    @staticmethod
    def _CreateTestData(
        sources=None,
        install_path=None,
        outputs=None,
        symlinks=None,
        recursive=False,
        options=None,
        command_type=None,
    ):
        """Generates a template of test data."""

        # It emulates a data that is generated by the templates in
        # //common-mk/BUILDCONFIG.gn for this BUILD.gn rule example
        #
        # group("all") {
        #   deps = ["//platform:install"]
        # }
        #
        # install_config("install") {
        #   sources = $sources
        #   install_path = $install_path
        #   outputs = $outputs
        #   symlinks = $symlinks
        #   recursive = $recursive
        #   options = $options
        #   type = $target_type
        #
        #   # some required variables
        # }
        install_config = {
            "sources": sources,
            "install_path": install_path,
            "outputs": outputs,
            "symlinks": symlinks,
            "recursive": recursive,
            "options": options,
            "type": command_type,
        }
        metadata = {"_install_config": [install_config]}
        return {
            TARGET_PREFIX
            + "all": {
                "deps": [
                    TARGET_PREFIX + "install",
                ],
            },
            TARGET_PREFIX
            + "install": {
                "metadata": metadata,
            },
        }

    @mock.patch("ebuild_function.generate", return_value=[["test", "command"]])
    def testEbuildParameter(self, generate_mock):
        """Makes sure the parameter passed to ebuild_function correctly."""
        gn_desc = self._CreateTestData(
            sources=["source"],
            install_path="/path",
            outputs=["output"],
            symlinks=["symlink"],
            recursive=True,
            options="-m0644",
            command_type="executable",
        )
        self._RunWithDesc("configure_install", gn_desc)
        generate_mock.assert_called_with(
            sources=["source"],
            install_path="/path",
            outputs=["output"],
            symlinks=["symlink"],
            recursive=True,
            options="-m0644",
            command_type="executable",
        )

    @mock.patch("ebuild_function.generate", return_value=[["test", "command"]])
    def testEbuildParameterWithoutNewNames(self, generate_mock):
        """Makes sure the parameter passed to ebuild_function correctly."""
        gn_desc = self._CreateTestData(
            sources=["source"],
            install_path="/path",
            recursive=True,
            options="-m0644",
            command_type="executable",
        )
        self._RunWithDesc("configure_install", gn_desc)
        generate_mock.assert_called_with(
            sources=["source"],
            install_path="/path",
            outputs=None,
            symlinks=None,
            recursive=True,
            options="-m0644",
            command_type="executable",
        )

    @mock.patch("ebuild_function.generate", return_value=[["test", "command"]])
    def testWithoutSources(self, generate_mock):
        """Tests configure_install without sources.

        Makes sure it returns empty list when sources aren't specified.
        """
        self._RunWithDesc("configure_install", self._CreateTestData())
        self.assertEqual(generate_mock.call_count, 0)

    @mock.patch("ebuild_function.generate", return_value=[["test", "command"]])
    def testWithSources(self, generate_mock):
        """Tests configure_install with sources.

        Makes sure it returns an install command when sources are specified.
        """
        gn_desc = self._CreateTestData(sources=["source"])
        self._RunWithDesc("configure_install", gn_desc)
        self.assertEqual(generate_mock.call_count, 1)

    @mock.patch("ebuild_function.generate", return_value=[["test", "command"]])
    def testMultipleDifferentPathCommands(self, generate_mock):
        """Tests configure_install when having different install_paths.

        Checks outputs are separated when having the different install_paths.
        """
        num_install = 10
        targets = [
            TARGET_PREFIX + "test%s" % str(i) for i in range(num_install)
        ]
        gn_desc = {
            TARGET_PREFIX
            + "all": {
                "deps": targets,
            },
        }
        for target in targets:
            gn_desc[target] = {
                "metadata": {
                    "_install_config": [
                        {
                            "install_path": target,
                            "sources": ["source"],
                        }
                    ],
                },
            }
        self._RunWithDesc("configure_install", gn_desc)
        self.assertEqual(generate_mock.call_count, 10)

    @mock.patch("ebuild_function.generate", return_value=[["test", "command"]])
    def testMultipleSamePathCommands(self, generate_mock):
        """Tests configure_install with commands having the same install_paths.

        Checks an output is combined when having the same install_paths.
        """
        num_install = 10
        targets = [
            TARGET_PREFIX + "test%s" % str(i) for i in range(num_install)
        ]
        gn_desc = {
            TARGET_PREFIX
            + "all": {
                "deps": targets,
            },
        }
        for target in targets:
            gn_desc[target] = {
                "metadata": {
                    "_install_config": [
                        {
                            "install_path": "/path",
                            "sources": ["source"],
                        }
                    ],
                },
            }
        self._RunWithDesc("configure_install", gn_desc)
        self.assertEqual(generate_mock.call_count, 1)

    @mock.patch("ebuild_function.generate", return_value=[["test", "command"]])
    def testMixedCommands(self, generate_mock):
        """Test configure_install when having both new-cmd and do-cmd.

        Checks it returns two commands when having both new-cmd and do-cmd.
        """
        # group("all") {
        #   deps = [
        #     "//platform:doins",
        #     "//platform:newins",
        #   ]
        # }
        #
        # install_config("doins") {
        #   sources = "source-doins"
        #   install_path = "/path"
        # }
        # install_config("newins") {
        #   sources = "source-oldins"
        #   install_path = "/path"
        #   outputs = "source-newins"
        # }

        doins_target = TARGET_PREFIX + "doins1"
        newins_target = TARGET_PREFIX + "newins"
        gn_desc = {
            TARGET_PREFIX
            + "all": {
                "deps": [
                    doins_target,
                    newins_target,
                ],
            },
            doins_target: {
                "metadata": {
                    "_install_config": [
                        {
                            "install_path": "/path",
                            "sources": ["source-doins"],
                        }
                    ],
                }
            },
            newins_target: {
                "metadata": {
                    "_install_config": [
                        {
                            "install_path": "/path",
                            "sources": ["source-oldins"],
                            "outputs": ["source-newins"],
                        }
                    ],
                },
            },
        }
        self._RunWithDesc("configure_install", gn_desc)
        generate_mock.assert_any_call(
            sources=["source-doins"],
            install_path="/path",
            outputs=None,
            symlinks=None,
            recursive=None,
            options=None,
            command_type=None,
        )
        generate_mock.assert_any_call(
            sources=["source-oldins"],
            install_path="/path",
            outputs=["source-newins"],
            symlinks=None,
            recursive=None,
            options=None,
            command_type=None,
        )


if __name__ == "__main__":
    # The tests in here are slow, but we don't have that many, so cap the
    # jobs value to avoid forking way more than needed.
    chromite_init.test_main(__file__, jobs=15)
