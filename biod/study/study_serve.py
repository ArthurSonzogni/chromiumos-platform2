#!/usr/bin/env python3
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Standalone local webserver to acquire fingerprints for user studies."""

from __future__ import annotations
from __future__ import print_function

import argparse
import datetime
import json
import logging
import logging.handlers
import os
import pathlib
import re
import subprocess
import sys
import threading
import time
from typing import Any, Literal, Optional, Union

# The following imports will be available on the test image, but will usually
# be missing in the SDK.
# pylint: disable=import-error
import cherrypy
import gnupg
from ws4py import messaging
from ws4py.server.cherrypyserver import WebSocketPlugin
from ws4py.server.cherrypyserver import WebSocketTool
from ws4py.websocket import WebSocket


# Use the image conversion library if available.
sys.path.extend(["/usr/local/opt/fpc", "/opt/fpc"])
try:
    import fputils as fpcutils
except ImportError:
    fpcutils = None

DEFAULT_ARGS = {
    "finger-count": 2,
    "enrollment-count": 20,
    "verification-count": 15,
    "port": 9000,
    "picture-dir": "./fingers",
    "syslog": False,
    "gpg-keyring": "",
    "gpg-recipients": "",
    "log-dir": "/var/log/fpstudy",
    "export-fmi": False,
}

errors = [
    # FP_SENSOR_LOW_IMAGE_QUALITY 1
    "Please try again by retrying...",
    # FP_SENSOR_TOO_FAST 2
    "Please try again by keeping your finger still during capture",
    # FP_SENSOR_LOW_SENSOR_COVERAGE 3
    "Please try again by centering your finger on the sensor",
]

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
HTML_DIR = os.path.join(SCRIPT_DIR, "html")

ECTOOL = "ectool"
# Wait to see a finger on the sensor.
FP_MODE_FINGER_DOWN = 2
# Poll until the finger has left the sensor.
FP_MODE_FINGER_UP = 4
# Capture the current finger image.
FP_MODE_CAPTURE = 8


class FingerWebSocket(WebSocket):
    """Handle the websocket used finger images acquisition and logging."""

    FP_MODE_RE = re.compile(r"^FP mode:\s*\(0x([0-9a-fA-F]+)\)", re.MULTILINE)
    DIR_FORMAT = "{participant:04d}/{group:s}/{finger:02d}"
    FILE_FORMAT = "{finger:02d}_{picture:02d}"

    config = {}
    pict_dir = pathlib.Path("/tmp")
    # fpcutils.FpUtils class to process images through the external library.
    fpcutils = None
    export_fmi = False
    # The optional GNUGPG instance used for encryption.
    gpg = None
    gpg_recipients: list = None
    # The worker thread processing the images.
    worker = None
    # The current request processed by the worker thread.
    current_req = None
    # The Condition variable the worker thread waits on to get a new request.
    available_req = threading.Condition()
    # Force terminating the current processing in the worker thread.
    abort_request = False

    def get_finger_capture_dir_path(self, req: dict[str, Any]) -> pathlib.Path:
        """Get the path to the current requested finger's capture directory."""
        directory = self.pict_dir / pathlib.Path(self.DIR_FORMAT.format(**req))
        directory.mkdir(parents=True, exist_ok=True)
        return directory

    def get_finger_capture_base_path(self, req: dict[str, Any]) -> pathlib.Path:
        """Get the file name/path for the current finger capture.

        This is the full path for the current finger and picture without the
        file extension.
        """
        directory = self.get_finger_capture_dir_path(req)
        file_base = directory / pathlib.Path(self.FILE_FORMAT.format(**req))
        return file_base

    def set_config(self, arg: argparse.Namespace):
        self.config = {
            "fingerCount": arg.finger_count,
            "enrollmentCount": arg.enrollment_count,
            "verificationCount": arg.verification_count,
        }
        self.pict_dir = pathlib.Path(arg.picture_dir)
        if fpcutils:
            self.fpcutils = fpcutils.FpUtils()
        if arg.export_fmi:
            self.export_fmi = True
        if arg.gpg_keyring:
            # The verbose flag prints a lot of info to console using print
            # directly. We use the logging interface instead.
            self.gpg = gnupg.GPG(
                keyring=arg.gpg_keyring,
                verbose=False,
                options=[
                    "--no-options",
                    "--no-default-recipient",
                    "--trust-model",
                    "always",
                ],
            )
            self.gpg_recipients = arg.gpg_recipients.split()
            if not self.gpg_recipients:
                cherrypy.log(
                    "Error - GPG Recipients is Empty", severity=logging.FATAL
                )
                cherrypy.engine.exit()
                return
            cherrypy.log(f"GPG Recipients: {self.gpg_recipients}")

            keyring_list = self.gpg.list_keys()
            if not keyring_list:
                cherrypy.log(
                    "Error - GPG Keyring is Empty", severity=logging.FATAL
                )
                cherrypy.engine.exit()
                return
            for k in keyring_list:
                cherrypy.log(f'GPG Keyring Key {k["fingerprint"]}:')
                for dk, dv in k.items():
                    if dv:
                        cherrypy.log(f"\t{dk}: {dv}")

            # Check if recipients are in the keyring and perfectly
            # match one to one. There could be a mismatch if a generic search
            # specifier is used for the recipient that matches more than one
            # key in the keyring.
            for recipients in self.gpg_recipients:
                keyring_list = self.gpg.list_keys(keys=recipients)
                if not (keyring_list and len(keyring_list) == 1):
                    cherrypy.log(
                        "Error - GPG Recipients do not match specific keys.",
                        severity=logging.FATAL,
                    )
                    cherrypy.engine.exit()
                    return

        self.worker = threading.Thread(target=self.finger_worker)
        self.worker.start()

    def closed(self, code, reason=""):
        self.abort_request = True
        cherrypy.log(f"Websocket closed with code {code} / {reason}")
        if not self.worker:
            cherrypy.log("Worker thread wasn't running.")
            return
        cherrypy.log("Stopping worker thread.")
        # Wake up the thread so it can exit.
        self.available_req.acquire()
        self.available_req.notify()
        self.available_req.release()
        self.worker.join(10.0)
        if self.worker.is_alive():
            cherrypy.log("Failed to stop worker thread.")
        else:
            cherrypy.log("Successfully stopped worker thread.")

    def received_message(
        self,
        message: Union[messaging.TextMessage, messaging.BinaryMessage],
    ):
        if message.is_binary:
            return  # Not supported
        # The JSON payloads from index.html are string indexed dictionaries.
        j: dict[str, Any] = json.loads(message.data)
        if "log" in j:
            cherrypy.log(j["log"])
        if "finger" in j:
            self.finger_request(j)
        if "config" in j:
            self.config_request(j)

    def make_dirs(self, path: str):
        if not os.path.exists(path):
            os.makedirs(path)

    def save_to_file(self, data: bytes, file_path: pathlib.Path):
        """Save data bytes to file at file_path.

        If GPG is enabled, the .gpg suffix is added to file_path.
        """

        if self.gpg:
            file_path = file_path.with_suffix(file_path.suffix + ".gpg")
            enc = self.gpg.encrypt(data, self.gpg_recipients)
            data = enc.data

        cherrypy.log(f"Saving file '{file_path}' size {len(data)}")
        if not data:
            cherrypy.log(
                "Error - Attempted to save empty file", severity=logging.ERROR
            )
            return

        file_path.write_bytes(data)

    def ectool(self, command: str, *args: str) -> bytes:
        """Run the ectool command and return its stdout as bytes."""

        cmdline = [ECTOOL, "--name=cros_fp", command] + list(args)
        while not self.abort_request:
            status = subprocess.run(cmdline, check=False, capture_output=True)
            if status.returncode == 0:
                return status.stdout
            else:
                cherrypy.log(
                    f"command '{status.args}' failed with {status.returncode}"
                )
        return b""

    def ectool_fpmode(self, *args: str) -> int:
        mode = self.ectool("fpmode", *args).decode("utf-8")
        match_mode = self.FP_MODE_RE.search(mode)
        return int(match_mode.group(1), 16) if match_mode else -1

    def finger_wait_done(self, mode: int) -> bool:
        """Wait for the bits in mode to be cleared from fpmode.

        Args:
            mode: An fpmode that we will wait to be cleared.

        Returns:
            False only when the request has been aborted. True, otherwise.
        """
        # Poll until the mode bit has disappeared.
        while not self.abort_request and self.ectool_fpmode() & mode:
            time.sleep(0.050)
        return not self.abort_request

    def capture(
        self, capture_mode: Literal["vendor", "pattern0", "pattern1"] = "vendor"
    ) -> tuple[Optional[bytes], Optional[str]]:
        """Capture one sample from the FPMCU.

        This function will start the capture, wait for the capture to complete,
        fetch + return the capture bytes.

        Args:
            capture_mode: "vendor", "pattern0", or "pattern1"

        Returns:
            The tuple of capture bytes and error result string.
            If the capture bytes are None, then an error occurred. Iin this case
            the optional error result string should be optionally passed to the
            client page.
        """
        # Capture the finger image when the finger is on the sensor.
        self.ectool_fpmode("capture", capture_mode)
        # Wait for the image to become available.
        if not self.finger_wait_done(FP_MODE_CAPTURE):
            cherrypy.log("Finger wait aborted.")
            return (None, None)
        # Download the image.
        if capture_mode == "vendor":
            img = self.ectool("fpframe", "raw")
        else:
            # The pattern0/1s are intended to be captured using "simple" mode
            # and will automatically be converted to PNM format.
            img = self.ectool("fpframe")

        if not img:
            msg = f"Failed to download fpframe for {capture_mode} capture"
            cherrypy.log(msg)
            return (None, f"{msg}. Call operator.")
        return (img, None)

    def finger_save_image(self, req: dict[str, Any], img: bytes):
        file_base = self.get_finger_capture_base_path(req)
        raw_file = file_base.with_suffix(".raw")
        fmi_file = file_base.with_suffix(".fmi")
        self.save_to_file(img, raw_file)
        if self.export_fmi:
            if self.fpcutils:
                rc, fmi = self.fpcutils.image_data_to_fmi(img)
                if rc == 0:
                    self.save_to_file(fmi, fmi_file)
                else:
                    cherrypy.log(f"FMI conversion failed {rc}")
            else:
                cherrypy.log("FPC utils are unavailable")

    def finger_setup(self, req: dict[str, Any]) -> Optional[str]:
        """Run once before capturing each finger.

        The user waits for this function to complete before capturing starts.
        The expectation is that the user does not have a finger on the sensor
        for this setup routine.

        This routine does the following:
        1. Capture a pattern0 and pattern1 sample and save it.
        2. Query the FPMCU running version and save it.
           Recording the version this frequently is probably not necessary,
           but it will ensure that we catch accidental version mismatches and
           changes across different test devices and different days of
           capturing.

        Args:
            req: The finger capture request from the client page.

        Returns:
            The optional result string to send to the client page.
            None when no result should be sent to the client page.
        """

        finger_dir = self.get_finger_capture_dir_path(req)

        t0 = time.time()
        cherrypy.log(f"Capturing pattern0 for finger {req['finger']:02d}")
        img_pattern0, result = self.capture("pattern0")
        t1 = time.time()
        cherrypy.log(f"Captured pattern0 in {t1 - t0:.2f}s")
        if not img_pattern0:
            return result
        self.save_to_file(img_pattern0, finger_dir / "pattern0.pnm")

        t0 = time.time()
        # The pattern1 capture is needed for Elan 80SG to do off-chip
        # evaluation.
        cherrypy.log(f"Capturing pattern1 for finger {req['finger']:02d}")
        img_pattern1, result = self.capture("pattern1")
        t1 = time.time()
        cherrypy.log(f"Captured pattern1 in {t1 - t0:.2f}s")
        if not img_pattern1:
            return result
        self.save_to_file(img_pattern1, finger_dir / "pattern1.pnm")

        t0 = time.time()
        cherrypy.log(
            f"Recording FPMCU FW version for finger {req['finger']:02d}"
        )
        version_string = self.ectool("version")
        t1 = time.time()
        self.save_to_file(version_string, finger_dir / "fpmcu_version.txt")
        cherrypy.log(f"Recorded FPMCU FW version in {t1 - t0:.2f}s")

        return "ok"

    def finger_process(self, req: dict[str, Any]) -> Optional[str]:
        """Capture and save fingerprint samples.

        Args:
            req: The finger capture request from the client page.

        Returns:
            The optional result string to send to the client page.
            None when no result should be sent to the client page.
        """
        # Ensure the user has removed the finger between 2 captures or
        # before the calibration capture occurs.
        # If the finger is already up, this mode will immediately be cleared.
        # Also, as a side effect, Elan's matcher will force a resample of
        # pattern1 upon fingerup, if the sample taken on boot contained a
        # fingerprint.
        self.ectool_fpmode("fingerup")
        if not self.finger_wait_done(FP_MODE_FINGER_UP):
            # Aborted. Don't send a result to client.
            return None

        if req["action"] == "setup":
            return self.finger_setup(req)

        if req["action"] != "sample":
            raise Exception(
                f"Received unexpected action '{req['action']}' from UI"
            )
        t0 = time.time()
        # Capture the finger image when the finger is on the sensor.
        img, result = self.capture("vendor")
        if not img:
            return result
        t1 = time.time()
        # Record the outcome of the capture.
        cherrypy.log(
            f'Captured finger {req["finger"]:02d}:{req["picture"]:02d}'
            f" in {t1 - t0:.2f}s"
        )
        self.finger_save_image(req, img)
        return "ok"

    def finger_worker(self):
        def send_result(result_msg: str = "ok"):
            """Send the result of the capture back to the user page.

            If the result is anything other than "ok", the page will show
            an error message box with the result message and the capture will
            be retried.

            This result field seems to be designed to accommodate in study
            feedback about the capture. The options were enumerated in the
            "errors" string list at the top of this python script.

            We will use it to surface general errors that should halt capturing,
            too.
            """
            self.current_req["result"] = result_msg
            # Tell the page about the acquisition result.
            self.send(json.dumps(self.current_req), False)

        while not self.server_terminated and not self.client_terminated:
            self.available_req.acquire()
            while not self.current_req and not self.abort_request:
                self.available_req.wait()
            result = self.finger_process(self.current_req)
            if result:
                send_result(result)
            self.current_req = None
            self.available_req.release()

    def finger_request(self, req: dict[str, Any]):
        # Ask the thread to exit the waiting loops
        # it will wait on the acquire() below if needed.
        self.abort_request = True
        # Ask the thread to process the new request.
        self.available_req.acquire()
        self.abort_request = False
        self.current_req = req
        self.available_req.notify()
        self.available_req.release()

    def config_request(self, req: dict[str, Any]):
        # Populate the configuration.
        req["config"] = self.config
        self.send(json.dumps(req), False)


class Root:
    """Serve the static HTML/CSS and connect the websocket."""

    def __init__(self, cmdline_args: argparse.Namespace):
        self.args = cmdline_args

    @cherrypy.expose
    def index(self):
        index_file = os.path.join(SCRIPT_DIR, "html/index.html")
        with open(index_file, encoding="utf-8") as f:
            return f.read()

    @cherrypy.expose
    def finger(self):
        cherrypy.request.ws_handler.set_config(self.args)


def environment_parameters(default_params: dict[str, Any]) -> dict[str, Any]:
    """Return |default_params| after overriding with environment vars.

    Given a dictionary of default runtime parameters, return the same
    dictionary with parameters overridden by their equivalent environment
    variable.

    A corresponding environment variable is the uppercase equivalent of the
    parameter name, with all '-' replaced with '_'.
    For examples, parameter "log-dir" corresponds to environment variable
    "LOG_DIR".
    """

    def parse_bool(bool_str: str) -> bool:
        if bool_str.lower() in {"true", "yes"}:
            return True
        elif bool_str.lower() in {"false", "no"}:
            return False
        else:
            raise ValueError(f"String '{bool_str}' can't be parsed to bool.")

    env_params = default_params.copy()
    for param in default_params:
        env_var = param.upper().replace("-", "_")
        arg_type = type(default_params[param])
        value = os.environ.get(env_var)
        if value is not None:
            try:
                if arg_type is bool:
                    value = parse_bool(value)
                elif arg_type is type(None):
                    raise Exception("Cannot handle type None in default list.")
                else:
                    value = arg_type(value)
            except ValueError:
                raise ValueError(env_var)
            env_params[param] = value
    return env_params


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()

    # Read environment variables as the arg default values.
    try:
        env_default = environment_parameters(DEFAULT_ARGS)
    except ValueError as e:
        parser.error(f"failed to parse {e}")

    # Get study parameters from the command-line.
    parser.add_argument(
        "-f",
        "--finger-count",
        type=int,
        default=env_default["finger-count"],
        help="Number of fingers acquired per user",
    )
    parser.add_argument(
        "-e",
        "--enrollment-count",
        type=int,
        default=env_default["enrollment-count"],
        help="Number of enrollment images per finger",
    )
    parser.add_argument(
        "-v",
        "--verification-count",
        type=int,
        default=env_default["verification-count"],
        help="Number of verification images per finger",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=env_default["port"],
        help="The port for the webserver",
    )
    parser.add_argument(
        "-d",
        "--picture-dir",
        default=env_default["picture-dir"],
        help="Directory for the fingerprint captures",
    )
    parser.add_argument(
        "-l",
        "--log-dir",
        default=env_default["log-dir"],
        help="Log files directory",
    )
    parser.add_argument(
        "-s",
        "--syslog",
        action="store_true",
        default=env_default["syslog"],
        help="Log to syslog",
    )
    parser.add_argument(
        "-k",
        "--gpg-keyring",
        type=str,
        default=env_default["gpg-keyring"],
        help="Path to the GPG keyring",
    )
    parser.add_argument(
        "-r",
        "--gpg-recipients",
        type=str,
        default=env_default["gpg-recipients"],
        help="User IDs of GPG recipients separated by space",
    )
    parser.add_argument(
        "-fmi",
        "--export-fmi",
        action="store_true",
        default=env_default["export-fmi"],
        help="Enable export of FMI converted capture.",
    )
    args = parser.parse_args(argv)

    # GPG can only be used when gpg-keyring and gpg-recipient are specified.
    if args.gpg_keyring and not args.gpg_recipients:
        parser.error("gpg-recipients must be specified with gpg-keyring")
    if args.gpg_recipients and not args.gpg_keyring:
        parser.error("gpg-keyring must be specified with gpg-recipients")
    if args.gpg_keyring and not os.access(args.gpg_keyring, os.R_OK):
        parser.error(f"cannot read gpg-keyring file {args.gpg_keyring}")

    # Configure cherrypy server.
    cherrypy.config.update({"server.socket_port": args.port})

    # Configure logging.
    cherrypy.config.update({"log.screen": False})

    loggers = []
    l = logging.getLogger("cherrypy.access")
    l.setLevel(logging.DEBUG)
    loggers.append(l)
    l = logging.getLogger("cherrypy.error")
    l.setLevel(logging.DEBUG)
    loggers.append(l)
    l = logging.getLogger("gnupg")
    l.setLevel(logging.INFO)
    loggers.append(l)

    if args.log_dir:
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        log_name = f"server-{timestamp}.log"
        h = logging.handlers.RotatingFileHandler(
            filename=os.path.join(args.log_dir, log_name)
        )
        for l in loggers:
            l.addHandler(h)
    if args.syslog:
        h = logging.handlers.SysLogHandler(
            address="/dev/log",
            facility=logging.handlers.SysLogHandler.LOG_LOCAL1,
        )
        for l in loggers:
            l.addHandler(h)
    if not args.log_dir and not args.syslog:
        h = logging.StreamHandler()
        for l in loggers:
            l.addHandler(h)

    WebSocketPlugin(cherrypy.engine).subscribe()
    cherrypy.tools.websocket = WebSocketTool()

    cherrypy.quickstart(
        Root(args),
        "/",
        config={
            "/finger": {
                "tools.websocket.on": True,
                "tools.websocket.handler_cls": FingerWebSocket,
            },
            "/static": {
                "tools.staticdir.on": True,
                "tools.staticdir.dir": HTML_DIR,
            },
        },
    )

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
