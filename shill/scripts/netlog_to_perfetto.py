#!/usr/bin/env python3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Parses system log files and converts them into a Perfetto trace file.

This script takes a system log file as input, parses each line to extract
timestamp, log level, process information, and message content. It then
converts these log entries into a Perfetto trace format (.pftrace),
which can be opened with the Perfetto UI (https://ui.perfetto.dev).
"""

import argparse
import datetime
import itertools
import os
import re
import sys
import uuid


try:
    from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import (
        BUILTIN_CLOCK_REALTIME,
    )
    from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import LogMessage
    from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import Trace
    from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import TracePacket
    from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import TrackEvent

    SEQ_INCREMENTAL_STATE_CLEARED = (
        TracePacket.SequenceFlags.SEQ_INCREMENTAL_STATE_CLEARED
    )
    SEQ_NEEDS_INCREMENTAL_STATE = (
        TracePacket.SequenceFlags.SEQ_NEEDS_INCREMENTAL_STATE
    )
except ImportError:
    print(
        "ERROR: Perfetto library not found. Please install it, e.g., "
        "'pip install perfetto'"
    )
    sys.exit(1)

TRUSTED_PACKET_SEQUENCE_ID = 80211

LOG_PRIORITY_MAP = {
    "DEBUG": LogMessage.Priority.PRIO_DEBUG,
    "ERROR": LogMessage.Priority.PRIO_ERROR,
    "FATAL": LogMessage.Priority.PRIO_FATAL,
    "INFO": LogMessage.Priority.PRIO_INFO,
    "UNSPECIFIED": LogMessage.Priority.PRIO_UNSPECIFIED,
    "UNUSED": LogMessage.Priority.PRIO_UNUSED,
    "VERBOSE": LogMessage.Priority.PRIO_VERBOSE,
    "WARNING": LogMessage.Priority.PRIO_WARN,
}


class SystemLogParser:
    """Parses a system log file line by line, extracting structured data.

    Attributes:
        log_file_path: Path to the input log file.
    """

    def __init__(self, log_file_path: str):
        """Initializes the SystemLogParser.

        Args:
            log_file_path: The path to the system log file to be parsed.
        """
        self.log_file_path = log_file_path
        # Pattern 1: Source location in brackets
        self.log_pattern_brackets = re.compile(
            r"^(?P<timestamp>\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z)\s+"
            r"(?P<log_level>\S+)\s+"
            r"(?P<process_name>[^\[\s]+)(?:\[(?P<pid>\d+)\])?:\s+"
            r"(?:(?:(?P<message_level>\S+)\s+)?"
            r"(?:(?P<message_process>\S+):)?\s+)?"
            r"\[(?P<source_path>[^:]+):(?P<line_number>\d+)\]\s+"
            r"(?P<message>.*)$"
        )
        # Pattern 2: No source location
        self.generic_log_pattern = re.compile(
            r"^(?P<timestamp>\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z)\s+"
            r"(?P<log_level>\S+)\s+"
            r"(?P<process_name>[^\[\s]+)(?:\[(?P<pid>\d+)\])?:\s+"
            r"(?P<message>.*)$"
        )

    def _parse_log_line(self, line: str):
        """Parses a single log line against known patterns.

        Tries multiple regex patterns to extract timestamp, log level,
        process info, source location (if available), and the message.

        Args:
            line: The log line string to parse.

        Returns:
            A dictionary containing the parsed fields (timestamp_ns, log_level,
            process_name, pid, message, source_path, line_number), or None
            if the line doesn't match any known pattern.
        """
        match = self.log_pattern_brackets.match(line)
        if not match:
            match = self.generic_log_pattern.match(line)
        if not match:
            return None

        data = match.groupdict()

        try:
            dt_obj = datetime.datetime.strptime(
                data["timestamp"], "%Y-%m-%dT%H:%M:%S.%fZ"
            )
            dt_obj = dt_obj.replace(tzinfo=datetime.timezone.utc)
            timestamp_ns = int(dt_obj.timestamp() * 1_000_000_000)
        except ValueError:
            return None

        log_level = data["log_level"]
        msg_level = data.get("message_level")
        if msg_level and msg_level.startswith("VERBOSE"):
            try:
                v_num = int(msg_level[7:])
                log_level = "DEBUG" if v_num <= 2 else "VERBOSE"
            except ValueError:
                log_level = "VERBOSE"

        pid = int(data["pid"]) if data["pid"] else 0

        message = data.get("message", "")

        source_path = data.get("source_path")
        if source_path:
            source_path = os.path.basename(source_path)

        line_number = (
            int(data["line_number"]) if data.get("line_number") else None
        )

        return {
            "timestamp_ns": timestamp_ns,
            "log_level": log_level,
            "process_name": data["process_name"],
            "pid": pid,
            "message": message,
            "source_path": source_path,
            "line_number": line_number,
        }

    def parse_logs(self, verbose: bool = False):
        """Parses the entire log file specified during initialization.

        Reads the file line by line and applies _parse_log_line to each line.

        Args:
            verbose: If True, print unmatched lines to stdout.

        Yields:
            A dictionary for each successfully parsed log entry.
        """
        try:
            with open(self.log_file_path, "r", encoding="utf-8") as f:
                for line in f:
                    parsed_data = self._parse_log_line(line)
                    if parsed_data:
                        yield parsed_data
                    else:
                        if verbose:
                            print(f"WARNING: Unmatched: {line.strip()}")
        except FileNotFoundError:
            print(f"ERROR: File not found '{self.log_file_path}'.")
            sys.exit(1)
        except Exception as e:
            print(f"ERROR: An error occurred while parsing the log file: {e}")
            sys.exit(1)


class PerfettoTraceConverter:
    """Converts parsed log entries into a Perfetto trace object.

    Handles interning of strings and source locations, creation of track
    descriptors, and generation of log event packets.
    """

    def __init__(self, parsed_log_generator):
        """Initializes the PerfettoTraceConverter.

        Args:
            parsed_log_generator: A generator yielding parsed log entry dicts.
        """
        self.parsed_log_generator = parsed_log_generator
        self.trace = Trace()
        self.process_to_thread_uuid = {}
        self.trusted_packet_sequence_id = TRUSTED_PACKET_SEQUENCE_ID
        self.string_to_iid = {}
        self.iid_counter = 1
        self.source_location_cache = {}  # (file_name, line_number) -> sl_iid
        self.sl_iid_counter = 1
        self.first_timestamp_ns = 0

    def _get_string_iid(self, s: str) -> int:
        """Gets or creates an interned ID for a string.

        Args:
            s: The string to intern.

        Returns:
            The interned integer ID for the string. Returns 0 for None or
            empty strings.
        """
        if not s:
            return 0
        if s not in self.string_to_iid:
            self.string_to_iid[s] = self.iid_counter
            self.iid_counter += 1
        return self.string_to_iid[s]

    def _get_source_location_iid(self, file_path: str, line_number: int) -> int:
        """Gets or creates an interned ID for a source code location.

        Combines file name and line number for interning.

        Args:
            file_path: The full path to the source file.
            line_number: The line number in the source file.

        Returns:
            The interned integer ID for the source location. Returns 0 if
            file_path is None or line_number is None.
        """
        if not file_path or line_number is None:
            return 0
        key = (file_path, line_number)
        if key not in self.source_location_cache:
            self.source_location_cache[key] = self.sl_iid_counter
            self.sl_iid_counter += 1
        return self.source_location_cache[key]

    def _add_track_descriptor(self, process_name, pid):
        """Adds process and thread track descriptors to the trace.

        Creates a main track for the process and a sub-track named "Logs"
        for the log messages.

        Args:
            process_name: The name of the process.
            pid: The process ID.

        Returns:
            The UUID of the "Logs" thread track.
        """
        key = (process_name, pid)
        if key in self.process_to_thread_uuid:
            return self.process_to_thread_uuid[key]

        process_uuid = uuid.uuid4().int & (1 << 64) - 1
        thread_uuid = uuid.uuid4().int & (1 << 64) - 1

        desc_timestamp = self.first_timestamp_ns

        packet_proc = self.trace.packet.add(
            timestamp=desc_timestamp,
            timestamp_clock_id=BUILTIN_CLOCK_REALTIME,
            trusted_packet_sequence_id=self.trusted_packet_sequence_id,
        )
        desc_proc = packet_proc.track_descriptor
        desc_proc.uuid = process_uuid
        proc_desc = desc_proc.process
        proc_desc.pid = pid
        proc_desc.process_name = process_name

        packet_thread = self.trace.packet.add(
            timestamp=desc_timestamp,
            timestamp_clock_id=BUILTIN_CLOCK_REALTIME,
            trusted_packet_sequence_id=self.trusted_packet_sequence_id,
        )
        desc_thread = packet_thread.track_descriptor
        desc_thread.uuid = thread_uuid
        desc_thread.parent_uuid = process_uuid
        desc_thread.name = "Logs"
        td = desc_thread.thread
        td.pid = pid
        td.tid = pid

        self.process_to_thread_uuid[key] = thread_uuid
        return thread_uuid

    def _add_log_entry(self, log_entry: dict, interned_data):
        """Adds a single log entry as a TrackEvent to the Perfetto trace.

        Uses the LogMessage proto within a TrackEvent, associating it with
        the appropriate track.

        Args:
            log_entry: A dictionary representing a parsed log entry.
            interned_data: The trace's InternedData object, populated with new
                           unique strings and source locations from this entry.
        """
        track_uuid = self.process_to_thread_uuid.get(
            (log_entry["process_name"], log_entry["pid"])
        )
        if not track_uuid:
            return

        packet = self.trace.packet.add(
            timestamp=log_entry["timestamp_ns"],
            timestamp_clock_id=BUILTIN_CLOCK_REALTIME,
            trusted_packet_sequence_id=self.trusted_packet_sequence_id,
            sequence_flags=SEQ_NEEDS_INCREMENTAL_STATE,
        )
        track_event = packet.track_event
        track_event.type = TrackEvent.TYPE_INSTANT
        track_event.track_uuid = track_uuid

        log_level = log_entry["log_level"].upper()
        log_message = track_event.log_message

        track_event.name = log_level
        log_message.prio = LOG_PRIORITY_MAP.get(
            log_level, LogMessage.Priority.PRIO_INFO
        )

        track_event.categories.append("log")
        track_event.categories.append(log_entry["process_name"])

        before_iid = self.iid_counter
        body_iid = self._get_string_iid(log_entry["message"])
        if self.iid_counter > before_iid and body_iid > 0:
            interned_data.log_message_body.add(
                iid=body_iid, body=log_entry["message"]
            )

        if body_iid > 0:
            log_message.body_iid = body_iid

        before_sl_iid = self.sl_iid_counter
        sl_iid = self._get_source_location_iid(
            log_entry["source_path"], log_entry["line_number"]
        )
        if self.sl_iid_counter > before_sl_iid and sl_iid > 0:
            interned_data.source_locations.add(
                iid=sl_iid,
                file_name=log_entry["source_path"],
                line_number=log_entry["line_number"],
            )
        if sl_iid > 0:
            log_message.source_location_iid = sl_iid

    def convert_and_save(self, output_file: str):
        """Converts all parsed logs and saves them to a Perfetto trace file.

        Orchestrates the addition of clock snapshots, interned data,
        track descriptors, and log events to the trace.

        Args:
            output_file: The path to save the output .pftrace file.
        """
        try:
            first_log_entry = next(self.parsed_log_generator)
        except StopIteration:
            print(
                "WARNING: No log entries to convert. Perfetto file not created."
            )
            return

        self.first_timestamp_ns = first_log_entry["timestamp_ns"]

        # --- Clock Snapshot ---
        clock_packet = self.trace.packet.add(
            timestamp=0,
            trusted_packet_sequence_id=self.trusted_packet_sequence_id,
        )
        clock_snapshot = clock_packet.clock_snapshot
        clock_snapshot.clocks.add(
            clock_id=BUILTIN_CLOCK_REALTIME, timestamp=self.first_timestamp_ns
        )
        clock_snapshot.primary_trace_clock = BUILTIN_CLOCK_REALTIME

        # --- Interned Data Packet ---
        intern_packet = self.trace.packet.add(
            timestamp=self.first_timestamp_ns,
            timestamp_clock_id=BUILTIN_CLOCK_REALTIME,
            trusted_packet_sequence_id=self.trusted_packet_sequence_id,
            sequence_flags=SEQ_INCREMENTAL_STATE_CLEARED,
        )
        interned_data = intern_packet.interned_data

        added_track_descriptors = set()
        processed_entries = 0

        full_log_iter = itertools.chain(
            [first_log_entry], self.parsed_log_generator
        )
        for log_entry in full_log_iter:
            processed_entries += 1
            # --- Track Descriptors ---
            proc_key = (log_entry["process_name"], log_entry["pid"])
            if proc_key not in added_track_descriptors:
                self._add_track_descriptor(
                    log_entry["process_name"], log_entry["pid"]
                )
                added_track_descriptors.add(proc_key)

            # --- Log Event ---
            self._add_log_entry(log_entry, interned_data)

        try:
            with open(output_file, "wb") as f:
                f.write(self.trace.SerializeToString())
            print(f"INFO: Perfetto trace saved to '{output_file}'")
            print(f"INFO: Processed {processed_entries} log entries.")
        except Exception as e:
            print(f"ERROR: Could not write output file '{output_file}': {e}")
            sys.exit(1)


def main():
    """Handles command-line arguments and orchestrates the conversion.

    Parses arguments, initializes parser and converter, and saves the output.
    """
    parser = argparse.ArgumentParser(
        description="Convert system net.log files to Perfetto traces."
    )
    parser.add_argument("logfile", help="Path to the input system log file.")
    parser.add_argument(
        "-o",
        "--output",
        help=(
            "Path to the output Perfetto trace file (.pftrace). "
            "Defaults to the input file name with .pftrace extension."
        ),
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose output.",
    )

    args = parser.parse_args()

    input_log_file = args.logfile
    output_file_name = args.output

    if not output_file_name:
        output_file_name = f"{os.path.splitext(input_log_file)[0]}.pftrace"
    else:
        _, ext = os.path.splitext(output_file_name)
        if not ext:
            output_file_name += ".pftrace"

    if args.verbose:
        print(f"INFO: Parsing log file: {input_log_file}")

    log_parser = SystemLogParser(input_log_file)
    parsed_data_generator = log_parser.parse_logs(args.verbose)

    converter = PerfettoTraceConverter(parsed_data_generator)
    converter.convert_and_save(output_file_name)


if __name__ == "__main__":
    main()
