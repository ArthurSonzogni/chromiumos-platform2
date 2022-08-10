#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to instrument tracing (remotely) on a DUT."""

import argparse
import logging
import shlex
import signal
import subprocess
import sys
import tempfile
from typing import Optional


def generate_trace_config(buffer_size_kb: int, fill_policy: str) -> str:
    """Generates the trace config."""

    # TODO: Allow customized categories.
    # By default enable only the camera trace events.
    enabled_track_event_categories = "camera.*"
    disabled_track_event_categories = "*"

    return f"""
# Buffer 0
buffers: {{
    size_kb: {buffer_size_kb}
    fill_policy: {fill_policy}
}}

# Buffer 1
buffers: {{
    size_kb: 4096
    fill_policy: DISCARD
}}

data_sources: {{
    config {{
        name: "track_event"
        target_buffer: 0
        track_event_config {{
            enabled_categories: "{enabled_track_event_categories}"
            disabled_categories: "{disabled_track_event_categories}"
        }}
    }}
}}

data_sources: {{
    config {{
        name: "org.chromium.trace_event"
        target_buffer: 0
        chrome_config {{
            trace_config: "{{\\"record_mode\\":\\"record-until-full\\",\\"included_categories\\":[\\"camera\\"],\\"memory_dump_config\\": {{}}}}"
        }}
    }}
}}

# Event-driven recording of frequency and idle state changes.

data_sources: {{
    config {{
        name: "linux.ftrace"
        target_buffer: 0
        ftrace_config {{
            buffer_size_kb: 16384
            drain_period_ms: 250
            ftrace_events: "ftrace/print"
            ftrace_events: "power/cpu_frequency"
            ftrace_events: "power/cpu_idle"
            ftrace_events: "power/suspend_resume"
            #ftrace_events: "sched/sched_switch"
            #ftrace_events: "sched/sched_process_exit"
            #ftrace_events: "sched/sched_process_free"
            #ftrace_events: "task/task_newtask"
            #ftrace_events: "task/task_rename"
        }}
    }}
}}

# Polling the current cpu frequency.

data_sources: {{
    config {{
        name: "linux.sys_stats"
        target_buffer: 0
        sys_stats_config {{
            cpufreq_period_ms: 500
            meminfo_period_ms: 1000
            meminfo_counters: MEMINFO_MEM_TOTAL
            meminfo_counters: MEMINFO_MEM_FREE
            meminfo_counters: MEMINFO_MEM_AVAILABLE
            vmstat_period_ms: 1000
            vmstat_counters: VMSTAT_NR_FREE_PAGES
            vmstat_counters: VMSTAT_NR_ALLOC_BATCH
            vmstat_counters: VMSTAT_NR_INACTIVE_ANON
            vmstat_counters: VMSTAT_NR_ACTIVE_ANON
            stat_period_ms: 2500
            stat_counters: STAT_CPU_TIMES
            stat_counters: STAT_FORK_COUNT
        }}
    }}
}}

# Reporting the list of available frequency for each CPU.

data_sources {{
    config {{
        name: "linux.system_info"
        target_buffer: 1
    }}
}}

# This is to get full process name and thread<>process relationships.

data_sources: {{
    config {{
        name: "linux.process_stats"
        target_buffer: 1
        process_stats_config {{
            scan_all_processes_on_start: true
            record_thread_names: true
        }}
    }}
}}
"""


def Cmd(cmd: list, remote: Optional[str]) -> list:
    """Helper function to quote args when needed."""
    ret = cmd
    if remote:
        ret = ["ssh", remote] + [shlex.quote(c) for c in cmd]
    logging.debug("Command: %s", ret)
    return ret


class PerfettoSession:
    """PerfettoSession represents a tracing session."""

    def __init__(self, args):
        fill_policy = "RING_BUFFER"
        if args.duration_sec is not None:
            fill_policy = "DISCARD"

        self.trace_config = generate_trace_config(
            args.buffer_size_kb, fill_policy
        )
        self.output_file = args.output_file
        self.remote = args.remote or None
        self.duration_sec = args.duration_sec or None

        # Temp file for storing the generated trace config. When running on a
        # remote DUT, the same temp filename will be used to create a temp
        # config file on the remote DUT.
        # pylint: disable=R1732
        self.tmp_cfg_file = tempfile.NamedTemporaryFile(prefix="trace_config-")

        # Temp file for trace event output. When running on a remote DUT, the
        # same temp filename will be used to create a temp config file on the
        # remote DUT.
        # pylint: disable=R1732
        self.tmp_out_file = tempfile.NamedTemporaryFile(prefix="trace-")

        self.proc = None
        self.pid = None
        self.interrupted = False

    def Start(self):
        """Starts the Perfetto tracing session.

        Generates the trace configs and starts the Perfetto process (remotely).
        """
        # Create trace config file
        perfetto_cmd = [
            "perfetto",
            "-c",
            self.tmp_cfg_file.name,
            "--txt",
            "-o",
            self.tmp_out_file.name,
        ]
        self.tmp_cfg_file.write(bytes(self.trace_config, "utf-8"))
        self.tmp_cfg_file.flush()
        if self.remote is not None:
            subprocess.run(
                ["scp", self.tmp_cfg_file.name, "%s:/tmp/" % self.remote],
                check=True,
            )
        logging.debug("Trace config file: %s", self.tmp_cfg_file.name)

        # pylint: disable=R1732
        self.proc = subprocess.Popen(
            Cmd(perfetto_cmd, self.remote), start_new_session=True
        )

        try:
            output = subprocess.run(
                Cmd(
                    ["pgrep", "-f", " ".join(perfetto_cmd)],
                    remote=self.remote,
                ),
                check=True,
                encoding="utf-8",
                stdout=subprocess.PIPE,
            )
        except subprocess.CalledProcessError:
            raise ProcessLookupError("Cannot find perfetto pid")
        self.pid = output.stdout.strip()

        logging.info(
            "Started recording new trace (REMOTE=%s, PID=%s)...",
            self.remote,
            self.pid,
        )

    def Wait(self):
        """Waits for the Perfetto process to end.

        The function waits until either SIGINT is received or after
        collecting trace for |self.duration_sec| seconds.
        """

        if self.proc is None:
            raise RuntimeError("Perfetto process not started yet")
        try:
            if self.duration_sec is None:
                logging.info("Ctrl+C to stop tracing")
            else:
                logging.info("Will record for %f second(s)", self.duration_sec)
            self.proc.wait(self.duration_sec)
        except subprocess.TimeoutExpired:
            self.Interrupt()

    def Interrupt(self):
        """Stops the Perfetto process.

        Gracefully terminates the Perfetto process through SIGINT.
        """

        logging.info("Stopped recording trace")
        subprocess.run(
            Cmd(["kill", "-SIGINT", self.pid], self.remote), check=True
        )

    def CleanUp(self):
        """Cleans up temp files and syncs the trace output."""

        if self.proc is None:
            raise RuntimeError("Perfetto process not started yet")

        self.proc.wait()
        logging.debug(
            "Perfetto process terminated with code %d", self.proc.returncode
        )

        logging.info("Syncing trace output file...")
        if self.remote:
            # Copy the output trace file back to host.
            subprocess.run(
                [
                    "scp",
                    "%s:%s" % (self.remote, self.tmp_out_file.name),
                    self.output_file,
                ],
                check=True,
            )
            # Clean up all the temp files on the DUT.
            subprocess.run(
                Cmd(
                    [
                        "rm",
                        "-f",
                        self.tmp_cfg_file.name,
                        self.tmp_out_file.name,
                    ],
                    remote=self.remote,
                ),
                check=True,
            )
        else:
            subprocess.run(
                ["cp", self.tmp_out_file.name, self.output_file], check=True
            )
        logging.info("Trace output written to: %s", self.output_file)

        self.tmp_cfg_file.close()
        self.tmp_out_file.close()

    def __enter__(self):
        self.Start()
        return self

    def __exit__(self, *_):
        self.CleanUp()


def main(argv: list):
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--debug", action="store_true", help="Enable debug logs"
    )

    subparsers = parser.add_subparsers(title="subcommands", dest="subcmd")

    # `Record` subcommand.
    record_parser = subparsers.add_parser(
        "record",
        description="Record a new trace",
        help="Record a new trace",
    )
    record_parser.add_argument(
        "-r",
        "--remote",
        type=str,
        default=None,
        help="Remote SSH DUT to trace (default is to trace locally)",
    )
    record_parser.add_argument(
        "-o",
        "--output_file",
        type=str,
        default="/tmp/perfetto-trace",
        help="Output trace file path (default=%(default)s)",
    )
    record_parser.add_argument(
        "-t",
        "--duration_sec",
        type=float,
        default=None,
        help="Duration in seconds to trace (default is to trace until Ctrl+C)",
    )
    record_parser.add_argument(
        "-b",
        "--buffer_size_kb",
        type=int,
        default=65536,
        help="Size of trace buffer in KB",
    )

    args = parser.parse_args(argv)

    log_level = logging.INFO
    if args.debug:
        log_level = logging.DEBUG
    logging.basicConfig(
        level=log_level, format="%(asctime)s %(levelname)s: %(message)s"
    )

    if args.subcmd == "record":
        with PerfettoSession(args) as s:
            # Capture SIGINT (KeyboardInterrupt from Ctrl+C).
            signal.signal(signal.SIGINT, lambda _, __: s.Interrupt())
            s.Wait()

    # TODO: Generate metrics with trace processor


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
