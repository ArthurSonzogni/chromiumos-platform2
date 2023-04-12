# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module encapsulates various tasks for Fake HAL setup."""

import logging
import pathlib
import shutil


_PERSISTENT_CONFIG_PATH = pathlib.Path("/etc/camera/fake_hal.json")
_CONFIG_PATH = pathlib.Path("/run/camera/fake_hal.json")


def persist():
    """Persists the config file for Fake HAL."""

    if _CONFIG_PATH.exists():
        logging.info(
            "Copy config from %s to %s", _CONFIG_PATH, _PERSISTENT_CONFIG_PATH
        )
        shutil.copy2(_CONFIG_PATH, _PERSISTENT_CONFIG_PATH)
    elif _PERSISTENT_CONFIG_PATH.exists():
        logging.info(
            "Remove persistent Fake HAL config %s", _PERSISTENT_CONFIG_PATH
        )
        _PERSISTENT_CONFIG_PATH.unlink()
    else:
        logging.info("No config found, nothing to persist.")
