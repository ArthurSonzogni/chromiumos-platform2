#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Meson's exe_wrapper to execute foreign-architecture binaries."""

import os
import sys
from typing import Dict, List, Mapping, NoReturn


# Several meson packages pass the path to the build or source
# directory into their tests using an environment variable so they
# can read data files or execute other test programs. These paths
# will be from the perspective of the cros chroot, not from the
# build sysroot that the test is actually going to run from. We
# can translate these paths by just stripping the ${SYSROOT}
# prefix from the start.
#
# Unfortunately there doesn't seem to be any standard approach to
# this, so we just have to look at all the env variables we have
# observed being used in this way.
_PATH_ENV_VARS = (
    # Used by dev-libs/wayland
    "TEST_BUILD_DIR",
    "TEST_SRC_DIR",
    # Used by media-libs/harfbuzz, dev-libs/json-glib, app-arch/gcab,
    # dev-libs/glib
    "G_TEST_BUILDDIR",
    "G_TEST_SRCDIR",
    # Used by x11-libs/libxkbcommon
    "top_builddir",
    "top_srcdir",
)


def _maybe_escape_sandbox(argv: List[str]) -> None:
    """Re-executes itself if necessary to escape from the Portage sandbox."""
    if os.environ.get("SANDBOX_ON") != "1":
        return

    os.environ["SANDBOX_ON"] = "0"
    os.environ.pop("LD_PRELOAD")
    os.execvp(argv[0], argv)


def _detect_qemu_arch(sysroot: str) -> str:
    """Detects the architecture of the sysroot."""
    if os.path.lexists(os.path.join(sysroot, "lib64/ld-linux-x86-64.so.2")):
        return "x86_64"
    if os.path.lexists(os.path.join(sysroot, "lib64/ld-linux-aarch64.so.1")):
        return "aarch64"
    if os.path.lexists(os.path.join(sysroot, "lib/ld-linux-armhf.so.3")):
        return "arm"
    raise RuntimeError(f"Unsupported arch: {sysroot}")


def _translate_path(path: str, sysroot: str) -> str:
    """Remove the SYSROOT prefix from paths that have it."""
    if path == sysroot:
        return "/"
    if path.startswith(sysroot):
        return path[len(sysroot) :]
    return path


def _translate_paths_in_env(
    orig_env: Mapping[str, str], sysroot: str
) -> Dict[str, str]:
    """Translates paths contained in environment variables."""
    new_env = {}
    for key, value in orig_env.items():
        if key in _PATH_ENV_VARS:
            value = _translate_path(value, sysroot)
        new_env[key] = value
    return new_env


def main(argv: List[str]) -> NoReturn:
    # This script MUST NOT depend on chromite as it's used to build primordial
    # third-party packages and thus introduces deep dependencies to chromite.
    assert "chromite" not in sys.modules

    # TODO: Don't escape from the sandbox. We do it for a historical reason:
    # this script used to call into platform2_test.py which unconditionally
    # escaped from the sandbox. We should instead add necessary file paths to
    # the sandbox allowlist and re-enable the sandbox.
    _maybe_escape_sandbox(argv)

    sysroot = os.environ.get("SYSROOT")
    if not sysroot:
        raise RuntimeError("$SYSROOT is not set")
    if not sysroot.startswith("/"):
        raise RuntimeError("$SYSROOT must be absolute")
    if sysroot == "/":
        raise RuntimeError("$SYSROOT must not be /")
    sysroot = sysroot.rstrip("/")

    qemu_arch = _detect_qemu_arch(sysroot)

    new_args = argv[1:]
    if not new_args:
        raise RuntimeError("Needs arguments")
    new_args[0] = _translate_path(new_args[0], sysroot)
    new_cwd = _translate_path(os.getcwd(), sysroot)

    exec_args = [
        "proot",
        f"--rootfs={sysroot}",
        f"--qemu=qemu-{qemu_arch}",
        f"--cwd={new_cwd}",
    ] + new_args
    exec_env = _translate_paths_in_env(os.environ, sysroot)

    os.execvpe(exec_args[0], exec_args, exec_env)


if __name__ == "__main__":
    main(sys.argv)
