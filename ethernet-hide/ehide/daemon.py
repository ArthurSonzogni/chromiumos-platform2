# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A daemon base class."""

from __future__ import annotations

import abc
import enum
import logging
import os
import signal
import sys
import time
from typing import NoReturn, Optional


class State(enum.Enum):
    """An enum class for the ehide states."""

    OFF = "off"
    SET_UP = "set_up"
    ON = "on"
    TEAR_DOWN = "tear_down"

    @classmethod
    def from_str(cls, s: str) -> Optional[State]:
        """Creates a State enum from string.

        Args:
            s: The string.

        Returns:
            A state enum. If fails then return None.
        """
        for state in State:
            if state.value == s:
                return state
        return None


class Daemon(abc.ABC):
    """A abstract daemon class.

    When started, the daemon is forked twice. The first fork is to detach the
    process from the current terminal session, and the second fork is to ensure
    that the new process is not a session leader, so it can never acquire a
    controlling terminal again. This ensures its independence.

    When stop() is called, the process sends a SIGTERM to the running daemon
    with the pid recorded in |pidfile| and terminates the daemon.

    To use Daemon, subclass it and overrides the set_up(), tear_down() and
    optionally the loop() methods.

    Attributes:
        log_path: The path for logging.
        pidfile: The path to store the daemon pid.
        state_file: The path to store the daemon state.
        stdin: The path to redirect stdin to. Normally this should be /dev/null.
        stdout: The path to redirect stdout to.
        stderr: The path to redirect stderr to.
    """

    def __init__(
        self,
        pidfile: str,
        state_file: str,
        stdin: str = "/dev/null",
        stdout: str = "/dev/null",
        stderr: str = "/dev/null",
    ):
        self.stdin = stdin
        self.stdout = stdout
        self.stderr = stderr
        self.pidfile = pidfile
        self.state_file = state_file

    def start(self) -> None:
        """Starts the daemon."""
        state = self.get_state()
        if state != State.OFF:
            logging.error(
                "Expected state %s but got state %s.",
                State.OFF.value,
                state.value,
            )
            sys.exit(1)

        # Start the daemon.
        self._change_state_to(State.SET_UP)
        self._daemonize()
        signal.signal(signal.SIGTERM, self._on_sigterm)
        if self.set_up():
            self._change_state_to(State.ON)
            self.loop()
        else:
            logging.error("Failed to set up the daemon.")
            self.stop()
            # Sleep a while to make sure self.on_sigterm() is called.
            time.sleep(1)

    def stop(self) -> None:
        """Stops the daemon.

        Note that the process that runs stop() will be different from the
        daemon. The process that runs stop() will sends SIGTERM to the
        daemon process to kill it.
        """
        state = self.get_state()
        if state not in (State.SET_UP, State.ON):
            logging.error(
                "Expected state %s or %s but got state %s.",
                State.SET_UP.value,
                State.ON.value,
                state.value,
            )
            sys.exit(1)

        self._change_state_to(State.TEAR_DOWN)
        try:
            os.kill(self.read_pid(), signal.SIGTERM)
        except (OSError, TypeError) as e:
            logging.error(e)

    @abc.abstractmethod
    def set_up(self) -> bool:
        """Sets up the daemon.

        Override this method when subclassing Daemon. It will be called after
        the process is daemonized.

        Returns:
            A bool value indicates whether the setup is successful.
        """

    @abc.abstractmethod
    def tear_down(self) -> None:
        """Tears down before stopping.

        This method will be called before the daemon stops. Override this method
        when subclassing Daemon.
        """

    def loop(self) -> NoReturn:
        """An infinite loop for the daemon.

        Can be overridden to do customized things, such as daemon state check,
        network health check, etc.
        """
        while True:
            pass

    def get_state(self) -> State:
        """Gets the daemon state.

        Returns:
            The daemon state enum. If the state file does not exist or records
            invalid state, then returns State.OFF by default.
        """
        try:
            with open(self.state_file, "r", encoding="utf-8") as f:
                state_str = f.read().strip()
            state = State.from_str(state_str)
            if not state:
                state = State.OFF
                logging.warning(
                    "Read invalid state %s, treat it as %s.",
                    state_str,
                    state.value,
                )
        except IOError:
            state = State.OFF
        logging.info("Current state: %s.", state.value)
        return state

    def read_pid(self) -> Optional[int]:
        """Reads the daemon pid from the pid file.

        Returns:
            The daemon pid. If the read fails, returns None.
        """
        # Check the pidfile to see if the daemon already runs.
        try:
            with open(self.pidfile, "r", encoding="utf-8") as f:
                pid = int(f.read().strip())
        except IOError:
            pid = None
        return pid

    def _daemonize(self) -> None:
        """Daemonizes itself with double fork."""
        try:
            pid = os.fork()
            if pid > 0:
                # Exit first parent.
                sys.exit(0)
        except OSError as e:
            logging.error("Fork #1 failed: %s.", e)
            sys.exit(1)

        # Decouple from parent environment.
        os.chdir("/")
        os.setsid()
        os.umask(0)

        # Do second fork.
        try:
            pid = os.fork()
            if pid > 0:
                # Exit from second parent.
                sys.exit(0)
        except OSError as e:
            logging.error("Fork #2 failed: %s.", e)
            sys.exit(1)

        # Redirect standard file descriptors.
        sys.stdout.flush()
        sys.stderr.flush()
        with open(self.stdin, "rb") as si:
            os.dup2(si.fileno(), sys.stdin.fileno())
        with open(self.stdout, "ab", 0) as so:
            os.dup2(so.fileno(), sys.stdout.fileno())
        with open(self.stderr, "ab", 0) as se:
            os.dup2(se.fileno(), sys.stderr.fileno())

        self._write_pid()

    def _write_pid(self) -> None:
        """Writes the pid of the current process to the pid file."""
        with open(self.pidfile, "w", encoding="utf-8") as f:
            f.write(f"{os.getpid()}\n")

    def _del_pid(self) -> None:
        """Deletes the pid file."""
        try:
            os.remove(self.pidfile)
        except OSError:
            pass

    def _on_sigterm(self, signum, frame) -> None:
        """This method is used to handle the SIGTERM signal.

        It tears down the daemon, deletes the pid file, changes the daemon to
        state off and terminates the process.

        The handler parameter in signal.signal() requires two arguments, but
        we don't need to use these arguments, so we set the arguments to *_.

        Args:
            signum: The number of the signal received. Unused.
            frame: A frame object containing information about where the program
                was interrupted. Unused.
        """
        # Unused.
        del signum, frame
        self.tear_down()
        self._del_pid()
        self._change_state_to(State.OFF)
        sys.exit(0)

    def _change_state_to(self, state: State) -> None:
        """Changes the daemon state and writes to the state file.

        Args:
            state: The new state.
        """
        with open(self.state_file, "w", encoding="utf-8") as f:
            f.write(f"{state.value}\n")
        logging.info("State changed to %s.", state.value)
