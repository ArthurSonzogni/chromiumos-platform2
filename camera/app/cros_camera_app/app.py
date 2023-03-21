# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module provides CameraApp class to control CCA programmatically.

Use this module to write automation script in Python if the command-line tool
is not powerful enough in your use cases.
"""

import enum
import json
import pathlib
from typing import Optional

from cros_camera_app import chrome


_TEST_EXTENSION_URL = (
    "chrome-extension://behllobkkfkfnphdnhnkndlbkcpglgmj/"
    "_generated_background_page.html"
)

_CCA_URL = "chrome://camera-app/views/main.html"

# Evaluating this template would dynamically import a JS module via a blob
# string, and exposes it on window.ext.
_EXTENSION_JS_TEMPLATE = """
(async () => {
  const blob = new Blob([%s], {type: 'text/javascript'});
  const url = URL.createObjectURL(blob);
  try {
    window.ext = await import(url);
  } finally {
    URL.revokeObjectURL(url);
  }
})()
"""


class Facing(enum.Enum):
    """Camera facing.

    This is the Python version of the Facing enum in CCA. Here we use the more
    common front/back naming instead of the user/environment naming in CCA.
    """

    FRONT = enum.auto()
    BACK = enum.auto()
    EXTERNAL = enum.auto()

    def to_js_value(self) -> str:
        if self is Facing.FRONT:
            return "user"
        elif self is Facing.BACK:
            return "environment"
        elif self is Facing.EXTERNAL:
            return "external"
        else:
            raise Exception("Unexpected enum value %s" % self)


class Mode(enum.Enum):
    """Capture mode of CCA.

    This is the Python version of the Mode enum in CCA. Note that PORTRAIT mode
    might not be available on all devices.
    """

    PHOTO = enum.auto()
    VIDEO = enum.auto()
    SCAN = enum.auto()
    PORTRAIT = enum.auto()

    def to_js_value(self) -> str:
        return self.name.lower()


class CameraApp:
    """Remote connections to CCA and the test extension."""

    def __init__(self):
        """Initializes the instance."""
        self._cr = chrome.Chrome()
        self._ext = None

    @property
    def ext(self) -> chrome.Page:
        """The lazily-created connection to test extension page."""
        if self._ext is not None:
            return self._ext

        page = self._cr.attach(_TEST_EXTENSION_URL)
        extension_js_path = pathlib.Path(__file__).parent / "extension.js"
        with open(extension_js_path, encoding="utf-8") as f:
            code = _EXTENSION_JS_TEMPLATE % json.dumps(f.read())
            page.eval(code)
        return page

    def open(
        self,
        *,
        facing: Optional[Facing] = None,
        mode: Optional[Mode] = None,
    ) -> None:
        """Opens the camera app.

        Args:
            facing: The facing of the camera to be opened.
            mode: The target capture mode in app.
        """
        opts = {}
        if facing is not None:
            opts["facing"] = facing.to_js_value()
        if mode is not None:
            opts["mode"] = mode.to_js_value()
        self.ext.call("ext.cca.open", opts)

    def close(self) -> None:
        """Closes all the camera app windows."""
        self._cr.close_targets(_CCA_URL)
