# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module encapsulates various tasks on device."""

import getpass
import logging
import subprocess


# TODO(shik): Make this work with guest login. Currently, Chrome does not
# forward some command line flags when restarting the process in guest mode.
# TODO(shik): Login automatically.

_CHROME_DEV_CONF_PATH = "/etc/chrome_dev.conf"

_CHROME_FLAGS = [
    "--remote-debugging-port=0",
    "--load-extension=/usr/local/autotest/common_lib/cros/autotest_private_ext",
    (
        "--load-guest-mode-test-extension="
        "/usr/local/autotest/common_lib/cros/autotest_private_ext"
    ),
    "--allowlisted-extension-id=behllobkkfkfnphdnhnkndlbkcpglgmj",
    "--enable-oobe-test-api",
]

_CHROME_DEV_CONF_HEADER = "# Added by CCA command line tool"
_CHROME_DEV_CONF_FOOTER = "# End of CCA command line tool section"
_CHROME_DEV_CONF_CONTENT = "\n".join(
    ["", _CHROME_DEV_CONF_HEADER, *_CHROME_FLAGS, _CHROME_DEV_CONF_FOOTER, ""]
)


class DeviceError(Exception):
    """Failed to perform some action on the testing device."""


def is_rootfs_writable() -> bool:
    """Checks whether rootfs is writable.

    Returns:
        Whether rootfs is writable.
    """
    with open("/proc/mounts", encoding="utf-8") as f:
        for line in f:
            # Each line looks like:
            # /dev/root / ext2 rw,seclabel,relatime 0 0
            device, _, _, attrs, *_ = line.strip().split()
            if device == "/dev/root" and "rw" in attrs.split(","):
                return True
    return False


def setup_chrome_dev_conf():
    """Persists the testing switches info chrome_dev.conf."""
    if not is_rootfs_writable():
        logging.info(
            (
                "Skip updating %s because rootfs is not writable,"
                " you might need to run setup again after Chrome restarted."
                " It's recommended to enable writable rootfs by running"
                " `/usr/share/vboot/bin/make_dev_ssd.sh"
                " --remove_rootfs_verification`."
            ),
            _CHROME_DEV_CONF_PATH,
        )
        return

    with open(_CHROME_DEV_CONF_PATH, encoding="utf-8") as f:
        if _CHROME_DEV_CONF_CONTENT in f.read():
            return

    logging.info("Updating %s", _CHROME_DEV_CONF_PATH)
    with open(_CHROME_DEV_CONF_PATH, "a", encoding="utf-8") as f:
        f.write(_CHROME_DEV_CONF_CONTENT)


def restart_chrome():
    """Restarts Chrome with the testing switches."""
    logging.info("Restarting Chrome")
    cmd = [
        "dbus-send",
        "--system",
        "--type=method_call",
        "--dest=org.chromium.SessionManager",
        "/org/chromium/SessionManager",
        "org.chromium.SessionManagerInterface.EnableChromeTesting",
        "boolean:true",  # force_relaunch
        "array:string:%s" % ",".join(_CHROME_FLAGS),
        "array:string:",  # extra_environment_variables
    ]
    subprocess.check_call(cmd)


def setup():
    """Setups the device for automation prerequisites."""
    if getpass.getuser() != "root":
        raise DeviceError("setup() should be executed by root")

    setup_chrome_dev_conf()
    restart_chrome()
