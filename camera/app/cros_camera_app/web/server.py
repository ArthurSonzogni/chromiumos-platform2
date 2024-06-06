# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module creates a simple web camera page with http server."""

import functools
import http.server
import logging
import mimetypes
import os
from pathlib import Path
from typing import Optional
import urllib.parse


@functools.lru_cache(1)
def static_dir() -> Path:
    return Path(__file__).resolve().parent


_STATIC_EXTS = [".html", ".js", ".css"]


class Handler(http.server.SimpleHTTPRequestHandler):
    def _resolve_static_path(self) -> Optional[Path]:
        path = urllib.parse.urlparse(self.path).path
        if path == "/":
            path = "/index.html"
        path = Path(path).relative_to("/")

        if path.suffix not in _STATIC_EXTS:
            return None

        static_path = static_dir() / path
        if not static_path.is_file():
            return None

        return static_path

    def _send_404(self):
        self.send_response(404)
        self.end_headers()

    def do_GET(self):
        path = self._resolve_static_path()
        if path is None:
            self._send_404()
            return

        content_type = mimetypes.guess_type(path)[0]
        assert content_type is not None
        size = path.stat().st_size
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(size))
        self.end_headers()
        with open(path, "rb") as f:
            os.sendfile(self.wfile.fileno(), f.fileno(), 0, size)


def serve_forever():
    # TODO(shik): Make host and port configurable
    host = "0.0.0.0"
    port = 8000
    logging.info("Start at http://%s:%d", host, port)
    # TODO(shik): Support auto-opening the web page.
    with http.server.ThreadingHTTPServer((host, port), Handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            logging.debug("Got KeyboardInterrupt")
