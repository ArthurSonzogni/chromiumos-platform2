# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module creates a simple web camera page with http server."""

import functools
import http.server
import json
import logging
import mimetypes
import os
from pathlib import Path
import subprocess
from typing import Dict, Iterator, Optional
import urllib.parse


@functools.lru_cache(1)
def static_dir() -> Path:
    return Path(__file__).resolve().parent


_STATIC_EXTS = [".html", ".js", ".css"]


@functools.lru_cache(1)
def has_turbostat() -> bool:
    # pylint: disable=subprocess-run-check
    p = subprocess.run(
        ["which", "turbostat"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return p.returncode == 0


def run_turbostat() -> Iterator[Dict[str, float]]:
    cmd = [
        "turbostat",
        "--quiet",
        "--Summary",
        "--interval",
        "1",
        "--show",
        "PkgWatt,CorWatt,GFXWatt",  # The output order could be different
    ]
    with subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    ) as proc:
        stdout = proc.stdout
        assert stdout is not None
        header = stdout.readline().split()
        for line in stdout:
            values = (float(x) for x in line.split())
            yield dict(zip(header, values))


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

    def _handle_static(self) -> bool:
        path = self._resolve_static_path()
        if path is None:
            return False

        content_type = mimetypes.guess_type(path)[0]
        assert content_type is not None
        size = path.stat().st_size
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(size))
        self.end_headers()
        with open(path, "rb") as f:
            os.sendfile(self.wfile.fileno(), f.fileno(), 0, size)

        return True

    def _handle_turbostat(self) -> bool:
        if self.path != "/turbostat":
            return False

        self.send_response(200)
        self.send_header("Content-type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()

        try:
            if has_turbostat():
                for stat in run_turbostat():
                    message = f"data: {json.dumps(stat)}\n\n"
                    self.wfile.write(message.encode("utf-8"))
                    self.wfile.flush()
            else:
                self.wfile.write(b"event: unsupported\n")
                self.wfile.write(b"data: \n\n")
                self.wfile.flush()

        except BrokenPipeError:
            logging.info("Disconnected")

        return True

    def do_GET(self):
        if self._handle_static():
            return

        if self._handle_turbostat():
            return

        self._send_404()


def serve_forever(host: str, port: int):
    logging.info("Start at http://%s:%d", host, port)
    # TODO(shik): Support auto-opening the web page.
    with http.server.ThreadingHTTPServer((host, port), Handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            logging.debug("Got KeyboardInterrupt")
