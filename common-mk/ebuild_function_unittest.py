#!/usr/bin/env python3
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for ebuild_function.py

Default values follow the default values of ebuild (see manpage of ebuild).
https://dev.gentoo.org/~zmedico/portage/doc/man/ebuild.5.html
"""

from unittest import mock

import ebuild_function


_HACK_VAR_TO_DISABLE_ISORT = "hack"
# pylint: disable=wrong-import-position
import chromite_init  # pylint: disable=unused-import

from chromite.lib import cros_test_lib


class DoCommandTests(cros_test_lib.TestCase):
    """Tests of ebuild_function.do_command()."""

    def testSingleSource(self):
        ret = ebuild_function.do_command("ins", ["source"])
        self.assertEqual(ret, [["doins", "source"]])

    def testMultipleSources(self):
        ret = ebuild_function.do_command("ins", ["source1", "source2"])
        self.assertEqual(ret, [["doins", "source1", "source2"]])

    def testDoinsRecursive(self):
        ret = ebuild_function.do_command("ins", ["source"], recursive=True)
        self.assertEqual(ret, [["doins", "-r", "source"]])

    def testNotDoinsRecursive(self):
        # "recursive" is disabled in any install_type except "ins".
        ret = ebuild_function.do_command("bin", ["source"], recursive=True)
        self.assertEqual(ret, [["dobin", "source"]])
        ret = ebuild_function.do_command("exe", ["source"], recursive=True)
        self.assertEqual(ret, [["doexe", "source"]])

    def testDoInvalidInstallType(self):
        with self.assertRaises(ebuild_function.InvalidInstallTypeError):
            ebuild_function.do_command("invalid", ["source"])


class NewCommandTests(cros_test_lib.TestCase):
    """Tests of ebuild_function.new_command()."""

    def testSingleSource(self):
        ret = ebuild_function.new_command("ins", ["source"], ["output"])
        self.assertEqual(ret, [["newins", "source", "output"]])

    def testMultipleSources(self):
        ret = ebuild_function.new_command(
            "ins", ["source1", "source2"], ["output1", "output2"]
        )
        self.assertEqual(
            ret,
            [
                ["newins", "source1", "output1"],
                ["newins", "source2", "output2"],
            ],
        )

    def testDifferentLengthOutput(self):
        # The number of outputs differ from that of sources.
        with self.assertRaises(AssertionError):
            ebuild_function.new_command(
                "ins", ["source"], ["output1", "output2"]
            )

    def testNewInvalidInstall(self):
        # install_type is invalid
        with self.assertRaises(ebuild_function.InvalidInstallTypeError):
            ebuild_function.new_command("invalid", ["source"], ["output"])


class InstallTests(cros_test_lib.TestCase):
    """Tests of ebuild_function.install()."""

    @mock.patch("ebuild_function.sym_install")
    def testCallSymInstall(self, sym_install_mock):
        # sym_install should be called when symlinks are specified.
        ebuild_function.install("sym", ["source"], ["symlink"])
        self.assertTrue(sym_install_mock.called)

    @mock.patch("ebuild_function.new_command")
    def testCallNewCommand(self, new_command_mock):
        # new_command should be called when outputs are specified.
        ebuild_function.install("ins", ["source"], ["output"])
        self.assertTrue(new_command_mock.called)

    @mock.patch("ebuild_function.do_command")
    def testCallDoCommand(self, do_command_mock):
        # do_command should be called by default.
        ebuild_function.install("ins", ["source"])
        self.assertTrue(do_command_mock.called)


class OptionCmdTests(cros_test_lib.TestCase):
    """Tests of ebuild_function.option_cmd().

    Checks returned options for each install_type.
    """

    def testInstallOption(self):
        self.assertEqual(
            ebuild_function.option_cmd("ins", install_path="/"),
            [["insinto", "/"], ["insopts", "-m0644"]],
        )
        self.assertEqual(
            ebuild_function.option_cmd("ins", install_path="/etc/init"),
            [["insinto", "/etc/init"], ["insopts", "-m0644"]],
        )
        self.assertEqual(
            ebuild_function.option_cmd(
                "ins", install_path="/", options="-m0755"
            ),
            [["insinto", "/"], ["insopts", "-m0755"]],
        )

    def testExecutableOption(self):
        self.assertEqual(ebuild_function.option_cmd("bin"), [["into", "/usr"]])
        self.assertEqual(
            ebuild_function.option_cmd("bin", install_path="/"), [["into", "/"]]
        )

    def testSharedLibraryOption(self):
        self.assertEqual(
            ebuild_function.option_cmd("lib.so"), [["into", "/usr"]]
        )
        self.assertEqual(
            ebuild_function.option_cmd("lib.so", install_path="/"),
            [["into", "/"]],
        )

    def testStaticLibraryOption(self):
        self.assertEqual(
            ebuild_function.option_cmd("lib.a"), [["into", "/usr"]]
        )
        self.assertEqual(
            ebuild_function.option_cmd("lib.a", install_path="/"),
            [["into", "/"]],
        )

    def testSymlinkOption(self):
        self.assertEqual(ebuild_function.option_cmd("sym"), [])


class GenerateTests(cros_test_lib.TestCase):
    """Tests of ebuild_function.generate()."""

    def testInstall(self):
        # Normal install (command_type isn't specified)
        self.assertEqual(
            ebuild_function.generate(["source"], install_path="/x"),
            [["insinto", "/x"], ["insopts", "-m0644"], ["doins", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(["source"], install_path="/usr"),
            [["insinto", "/usr"], ["insopts", "-m0644"], ["doins", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="/x", options="-m0755"
            ),
            [["insinto", "/x"], ["insopts", "-m0755"], ["doins", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="/x", recursive=True
            ),
            [
                ["insinto", "/x"],
                ["insopts", "-m0644"],
                ["doins", "-r", "source"],
            ],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="/x", outputs=["output"]
            ),
            [
                ["insinto", "/x"],
                ["insopts", "-m0644"],
                ["newins", "source", "output"],
            ],
        )

    def testExecutableInstall(self):
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="bin", command_type="executable"
            ),
            [["into", "/usr"], ["dobin", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="sbin", command_type="executable"
            ),
            [["into", "/usr"], ["dosbin", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"],
                install_path="/opt/google",
                command_type="executable",
            ),
            [["exeinto", "/opt/google"], ["doexe", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="/bin", command_type="executable"
            ),
            [["into", "/"], ["dobin", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"],
                install_path="bin",
                outputs=["output"],
                command_type="executable",
            ),
            [["into", "/usr"], ["newbin", "source", "output"]],
        )

    def testSharedLibraryInstall(self):
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="lib", command_type="shared_library"
            ),
            [["into", "/usr"], ["dolib.so", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="/lib", command_type="shared_library"
            ),
            [["into", "/"], ["dolib.so", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"],
                install_path="lib",
                outputs=["output"],
                command_type="shared_library",
            ),
            [["into", "/usr"], ["newlib.so", "source", "output"]],
        )

    def testStaticLibraryInstall(self):
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="lib", command_type="static_library"
            ),
            [["into", "/usr"], ["dolib.a", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="/lib", command_type="static_library"
            ),
            [["into", "/"], ["dolib.a", "source"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"],
                install_path="lib",
                outputs=["output"],
                command_type="static_library",
            ),
            [["into", "/usr"], ["newlib.a", "source", "output"]],
        )

    def testSymlinkInstall(self):
        self.assertEqual(
            ebuild_function.generate(["source"], symlinks=["symlink"]),
            [["dosym", "source", "symlink"]],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], symlinks=["symlink"], install_path="/"
            ),
            [["dosym", "source", "/symlink"]],
        )

    def testInstallPathAlias(self):
        self.assertEqual(
            ebuild_function.generate(["source"], install_path="dbus_system_d"),
            [
                ["insinto", "/etc/dbus-1/system.d"],
                ["insopts", "-m0644"],
                ["doins", "source"],
            ],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="dbus_system_services"
            ),
            [
                ["insinto", "/usr/share/dbus-1/system-services"],
                ["insopts", "-m0644"],
                ["doins", "source"],
            ],
        )
        self.assertEqual(
            ebuild_function.generate(["source"], install_path="minijail_conf"),
            [
                ["insinto", "/usr/share/minijail"],
                ["insopts", "-m0644"],
                ["doins", "source"],
            ],
        )
        self.assertEqual(
            ebuild_function.generate(["source"], install_path="seccomp_policy"),
            [
                ["insinto", "/usr/share/policy"],
                ["insopts", "-m0644"],
                ["doins", "source"],
            ],
        )
        self.assertEqual(
            ebuild_function.generate(["source"], install_path="tmpfilesd"),
            [
                ["insinto", "/usr/lib/tmpfiles.d"],
                ["insopts", "-m0644"],
                ["doins", "source"],
            ],
        )
        self.assertEqual(
            ebuild_function.generate(
                ["source"], install_path="tmpfiled_ondemand"
            ),
            [
                ["insinto", "/usr/lib/tmpfiles.d/on-demand"],
                ["insopts", "-m0644"],
                ["doins", "source"],
            ],
        )
        self.assertEqual(
            ebuild_function.generate(["source"], install_path="upstart"),
            [
                ["insinto", "/etc/init"],
                ["insopts", "-m0644"],
                ["doins", "source"],
            ],
        )

    def testUnknownTypeInstall(self):
        # command_type is an unknown value
        with self.assertRaises(AssertionError):
            ebuild_function.generate(["source"], command_type="something")


if __name__ == "__main__":
    # All the tests in here are cheap, so don't spend time spinning up parallel
    # jobs to run them as it'll be slower (even a single fork is slower).
    chromite_init.test_main(__file__, jobs=0)
