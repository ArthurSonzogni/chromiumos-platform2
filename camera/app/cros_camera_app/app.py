# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module provides CameraApp class to control CCA programmatically.

Use this module to write automation script in Python if the command-line tool
is not powerful enough in your use cases.
"""

import base64
import contextlib
import dataclasses
import enum
import logging
import pathlib
import time
from typing import Any, Dict, Optional

from cros_camera_app import chrome
from cros_camera_app import device


_TEST_EXTENSION_URL = (
    "chrome-extension://behllobkkfkfnphdnhnkndlbkcpglgmj/"
    "_generated_background_page.html"
)

_CCA_URL = "chrome://camera-app/views/main.html"

# Calling this function will dynamically import a JS module via a blob string,
# and exposes it under `window` with the given name.
_IMPORT_JS_MODULE = """
async (name, code) => {
  const blob = new Blob([code], {type: 'text/javascript'});
  const url = URL.createObjectURL(blob);
  try {
    window[name] = await import(url);
  } finally {
    URL.revokeObjectURL(url);
  }
}
"""


def _script_path(file: str) -> pathlib.Path:
    return pathlib.Path(__file__).parent / file


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
            raise Exception(f"Unexpected enum value {self}")


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


@dataclasses.dataclass(frozen=True)
class Resolution:
    width: int
    height: int

    @classmethod
    def from_str(cls, s: str) -> "Resolution":
        w, h = s.split("x")
        return cls(int(w), int(h))

    def __str__(self) -> str:
        return f"{self.width}x{self.height}"

    def to_js_value(self) -> Dict[str, int]:
        return dataclasses.asdict(self)


def get_camera_file_watcher(pattern: str) -> device.FileWatcher:
    camera_dir = device.get_my_files_dir() / "Camera"
    return device.FileWatcher(camera_dir, pattern)


class CameraApp:
    """Remote connections to CCA and the test extension."""

    def __init__(self):
        """Initializes the instance."""
        self._cr = chrome.Chrome()
        self._ext: Optional[chrome.Page] = None
        self._page: Optional[chrome.Page] = None

    @property
    def ext(self) -> chrome.Page:
        """The lazily-created connection to test extension page."""
        if self._ext is not None and self._ext.is_alive:
            return self._ext

        page = self._cr.attach(_TEST_EXTENSION_URL)

        # The JavaScript code is encapsulated as a module here, so it's safe to
        # be evaluated multiple times in case the previous connection is
        # broken. The global `window.ext` would be overridden by the latest run.
        with open(_script_path("extension.js"), encoding="utf-8") as f:
            page.call(_IMPORT_JS_MODULE, "ext", f.read())

        return page

    @property
    def page(self) -> Optional[chrome.Page]:
        """The connection to CCA page. None if CCA is not running."""
        if self._page is not None and self._page.is_alive:
            return self._page

        page = self._cr.try_attach(_CCA_URL)
        self._page = page
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

        if self.page is None:
            raise Exception("Failed to open CCA")

        # A reload is necessary because `page.setBypassCSP` only takes effect
        # on the next navigation. Omitting this reload may appear to work
        # sometimes due to a race condition between the Content Security Policy
        # (CSP) initialization and the Chrome DevTools Protocol (CDP)
        # connection.
        self.page.rpc("Page.reload", {})
        with open(_script_path("app.js"), encoding="utf-8") as f:
            self.page.call(_IMPORT_JS_MODULE, "app", f.read())
        self.page.call("app.log", "App opened")

    def close(self) -> None:
        """Closes all the camera app windows."""
        self._cr.close_targets(_CCA_URL)

    @contextlib.contextmanager
    def session(
        self,
        *,
        facing: Optional[Facing] = None,
        mode: Optional[Mode] = None,
    ):
        """Context manager to start/stop a session automatically.

        Args:
            facing: The facing of the camera to be opened.
            mode: The target capture mode in app.

        Yields:
            The page that is connected to the session.
        """
        # TODO(shik): Reuse an existing CCA instance by switching to the
        # correct mode and facing. For now, close any existing CCA window
        # and restart from a clean state if facing or mode is specified.
        fresh = (
            (self.page is None) or (facing is not None) or (mode is not None)
        )
        if fresh:
            self.close()
            self.open(facing=facing, mode=mode)

        page = self.page
        if page is None:
            raise Exception("Page not found")

        try:
            yield page
        finally:
            if fresh:
                # Close CCA if it's a fresh instance opened for this session.
                self.close()

    def take_photo(self, *, facing: Optional[Facing] = None) -> pathlib.Path:
        """Takes a photo.

        Args:
            facing: The facing of the camera to be captured.

        Returns:
            The path of the captured photo.
        """
        with self.session(facing=facing, mode=Mode.PHOTO):
            watcher = get_camera_file_watcher("*.jpg")
            self.ext.call("ext.cca.takePhoto")
            return watcher.poll_new_file()

    def record_video(
        self,
        *,
        facing: Optional[Facing] = None,
        resolution: Optional[Resolution] = None,
        duration: float = 3,
    ) -> pathlib.Path:
        """Records a video.

        Args:
            facing: The facing of the camera to be recorded.
            resolution: The video resolution to be recorded.
            duration: The duration in seconds to be recorded.

        Returns:
            The path of the recorded video.
        """
        with self.session(facing=facing, mode=Mode.VIDEO) as page:
            if resolution is not None:
                logging.info("Set resolution to %s", resolution)
                page.call("app.setVideoResolution", resolution.to_js_value())

            watcher = get_camera_file_watcher("*.mp4")
            self.ext.call("ext.cca.startRecording")
            logging.info("Recording video for %g seconds", duration)
            time.sleep(duration)
            self.ext.call("ext.cca.stopRecording")
            return watcher.poll_new_file()

    def screenshot(self) -> bytes:
        """Takes a screenshot of the CCA window.

        Returns:
            The binary bytes of the captured screenshot PNG image.
        """
        with self.session() as page:
            res = page.rpc("Page.captureScreenshot", {"format": "png"})
            return base64.b64decode(res["data"])

    def eval(self, expr: str) -> Any:
        """Evaluates the JavaScript expression on CCA page.

        Args:
            expr: A JavaScript expression.

        Returns:
            The evaluated result. If it's a promise, the result would be
            automatically awaited.
        """
        with self.session() as page:
            return page.eval(expr)
