#!/usr/bin/env python3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for netlog_to_perfetto.py."""

import datetime
import os
import sys
import tempfile
import textwrap
import unittest
from unittest import mock
import uuid

import netlog_to_perfetto


try:
    from perfetto.protos.perfetto.trace import perfetto_trace_pb2
except ImportError:
    print(
        "ERROR: Perfetto library not found. Please install it, e.g., "
        "'pip install perfetto'"
    )
    sys.exit(1)


class SystemLogParserTest(unittest.TestCase):
    """Tests the SystemLogParser class."""

    def test_parse_log_line_bracket_pattern_verbose2(self):
        """Test VERBOSE2 in message_level, should become DEBUG."""
        parser = netlog_to_perfetto.SystemLogParser("sample_path.log")
        line = (
            "2025-08-14T12:26:55.410179Z DEBUG shill[1194]: VERBOSE2 shill: "
            "[../../../../../../../mnt/host/source/src/platform2/shill/wifi"
            "/wifi.cc:3446] /device/wlan0 WiFi wlan0 setting pending service "
            "to wifi_none_464"
        )
        expected = {
            "timestamp_ns": int(
                datetime.datetime(
                    2025,
                    8,
                    14,
                    12,
                    26,
                    55,
                    410179,
                    tzinfo=datetime.timezone.utc,
                ).timestamp()
                * 1e9
            ),
            "log_level": "DEBUG",
            "process_name": "shill",
            "pid": 1194,
            "message": (
                "/device/wlan0 WiFi wlan0 setting pending service to "
                "wifi_none_464"
            ),
            "source_path": "wifi.cc",
            "line_number": 3446,
        }
        # pylint: disable=protected-access
        result = parser._parse_log_line(line)
        self.assertEqual(result, expected)

    def test_parse_log_line_bracket_pattern_verbose3(self):
        """Test VERBOSE3 in message_level, should become VERBOSE."""
        parser = netlog_to_perfetto.SystemLogParser("sample_path.log")
        line = (
            "2025-08-14T12:26:55.410179Z DEBUG shill[1194]: VERBOSE3 shill: "
            "[../../../../../../../mnt/host/source/src/platform2/shill/wifi/"
            "wifi.cc:3446] /device/wlan0 WiFi wlan0 setting pending service "
            "to wifi_none_464"
        )
        expected = {
            "timestamp_ns": int(
                datetime.datetime(
                    2025,
                    8,
                    14,
                    12,
                    26,
                    55,
                    410179,
                    tzinfo=datetime.timezone.utc,
                ).timestamp()
                * 1e9
            ),
            "log_level": "VERBOSE",
            "process_name": "shill",
            "pid": 1194,
            "message": (
                "/device/wlan0 WiFi wlan0 setting pending service to "
                "wifi_none_464"
            ),
            "source_path": "wifi.cc",
            "line_number": 3446,
        }
        # pylint: disable=protected-access
        result = parser._parse_log_line(line)
        self.assertEqual(result, expected)

    def test_parse_log_line_bracket_pattern_info(self):
        """Test INFO in message_level, log_level should remain from outer."""
        parser = netlog_to_perfetto.SystemLogParser("sample_path.log")
        line = (
            "2025-08-14T12:26:55.411020Z INFO shill[1194]: INFO shill: "
            "[../../../../../../../mnt/host/source/src/platform2/shill/"
            "device.cc:444] wlan0 no_service sid=none "
            "SelectService(wifi_none_464)"
        )
        expected = {
            "timestamp_ns": int(
                datetime.datetime(
                    2025,
                    8,
                    14,
                    12,
                    26,
                    55,
                    411020,
                    tzinfo=datetime.timezone.utc,
                ).timestamp()
                * 1e9
            ),
            "log_level": "INFO",
            "process_name": "shill",
            "pid": 1194,
            "message": "wlan0 no_service sid=none SelectService(wifi_none_464)",
            "source_path": "device.cc",
            "line_number": 444,
        }
        # pylint: disable=protected-access
        result = parser._parse_log_line(line)
        self.assertEqual(result, expected)

    def test_parse_log_line_generic_pattern(self):
        """Test parsing a line matching the generic pattern."""
        parser = netlog_to_perfetto.SystemLogParser("sample_path.log")
        line = (
            "2025-08-14T12:27:10.632505Z DEBUG wpa_supplicant[693]: "
            "wlan0: RSN: Clearing AP RSNE Override element"
        )
        expected = {
            "timestamp_ns": int(
                datetime.datetime(
                    2025,
                    8,
                    14,
                    12,
                    27,
                    10,
                    632505,
                    tzinfo=datetime.timezone.utc,
                ).timestamp()
                * 1e9
            ),
            "log_level": "DEBUG",
            "process_name": "wpa_supplicant",
            "pid": 693,
            "message": "wlan0: RSN: Clearing AP RSNE Override element",
            "source_path": None,
            "line_number": None,
        }
        # pylint: disable=protected-access
        result = parser._parse_log_line(line)
        self.assertEqual(result, expected)

    def test_parse_log_line_unmatched(self):
        """Test a line that doesn't match any pattern."""
        parser = netlog_to_perfetto.SystemLogParser("sample_path.log")
        line = "This is not a valid log line"
        # pylint: disable=protected-access
        result = parser._parse_log_line(line)
        self.assertIsNone(result)

    def test_parse_logs_valid_file(self):
        """Test the parse_logs method with a mock file."""
        log_content = textwrap.dedent(
            """
            2025-08-14T12:26:17.529573Z DEBUG shill[1194]: [f.cc:195] M1
            2025-08-14T12:26:22.331067Z DEBUG wpa[693]: M2
            Invalid Line
            2025-08-14T12:26:37.912549Z INFO dhcpcd7[39769]: M3
        """
        ).strip()

        with mock.patch(
            "builtins.open", mock.mock_open(read_data=log_content)
        ) as mock_file:
            parser = netlog_to_perfetto.SystemLogParser("fake_file.log")
            results = list(parser.parse_logs())
            mock_file.assert_called_once_with(
                "fake_file.log", "r", encoding="utf-8"
            )

        self.assertEqual(
            len(results),
            3,
            msg="Expected to parse 3 valid lines, skipping the invalid one",
        )

        expected0 = {
            "timestamp_ns": int(
                datetime.datetime(
                    2025,
                    8,
                    14,
                    12,
                    26,
                    17,
                    529573,
                    tzinfo=datetime.timezone.utc,
                ).timestamp()
                * 1e9
            ),
            "log_level": "DEBUG",
            "process_name": "shill",
            "pid": 1194,
            "message": "M1",
            "source_path": "f.cc",
            "line_number": 195,
        }
        expected1 = {
            "timestamp_ns": int(
                datetime.datetime(
                    2025,
                    8,
                    14,
                    12,
                    26,
                    22,
                    331067,
                    tzinfo=datetime.timezone.utc,
                ).timestamp()
                * 1e9
            ),
            "log_level": "DEBUG",
            "process_name": "wpa",
            "pid": 693,
            "message": "M2",
            "source_path": None,
            "line_number": None,
        }
        expected2 = {
            "timestamp_ns": int(
                datetime.datetime(
                    2025,
                    8,
                    14,
                    12,
                    26,
                    37,
                    912549,
                    tzinfo=datetime.timezone.utc,
                ).timestamp()
                * 1e9
            ),
            "log_level": "INFO",
            "process_name": "dhcpcd7",
            "pid": 39769,
            "message": "M3",
            "source_path": None,
            "line_number": None,
        }

        self.assertEqual(results[0], expected0)
        self.assertEqual(results[1], expected1)
        self.assertEqual(results[2], expected2)

    @mock.patch("builtins.open", new_callable=mock.mock_open)
    def test_parse_logs_empty_file(self, mock_file):
        """Test parse_logs with an empty file."""
        mock_file.return_value.read.return_value = ""
        parser = netlog_to_perfetto.SystemLogParser("empty.log")
        results = list(parser.parse_logs())
        self.assertEqual(len(results), 0)

    @mock.patch("builtins.open")
    @mock.patch("builtins.print")
    @mock.patch("sys.exit")
    def test_parse_logs_file_not_found(self, mock_exit, mock_print, mock_open):
        """Test parse_logs when the file does not exist."""
        mock_open.side_effect = FileNotFoundError
        mock_exit.side_effect = SystemExit(1)
        parser = netlog_to_perfetto.SystemLogParser("nonexistent.log")
        with self.assertRaises(SystemExit) as cm:
            list(parser.parse_logs())
        self.assertEqual(cm.exception.code, 1)
        mock_open.assert_called_once_with(
            "nonexistent.log", "r", encoding="utf-8"
        )
        mock_print.assert_any_call("ERROR: File not found 'nonexistent.log'.")
        mock_exit.assert_called_once_with(1)

    @mock.patch("builtins.open")
    @mock.patch("builtins.print")
    @mock.patch("sys.exit")
    def test_parse_logs_other_exception(self, mock_exit, mock_print, mock_open):
        """Test parse_logs with a generic exception during file read."""
        mock_open.side_effect = IOError("Disk full")
        mock_exit.side_effect = SystemExit(1)
        parser = netlog_to_perfetto.SystemLogParser("faulty.log")
        with self.assertRaises(SystemExit) as cm:
            list(parser.parse_logs())
        self.assertEqual(cm.exception.code, 1)
        mock_open.assert_called_once_with("faulty.log", "r", encoding="utf-8")
        mock_print.assert_any_call(
            "ERROR: An error occurred while parsing the log file: Disk full"
        )
        mock_exit.assert_called_once_with(1)


class PerfettoTraceConverterTest(unittest.TestCase):
    """Tests the PerfettoTraceConverter class."""

    def setUp(self):
        self.sample_logs = [
            {
                "timestamp_ns": int(
                    datetime.datetime(
                        2025, 8, 14, 12, 0, 0, 0, tzinfo=datetime.timezone.utc
                    ).timestamp()
                    * 1e9
                ),
                "log_level": "INFO",
                "process_name": "shill",
                "pid": 100,
                "message": "Message 1",
                "source_path": "file1.cc",
                "line_number": 10,
            },
            {
                "timestamp_ns": int(
                    datetime.datetime(
                        2025, 8, 14, 12, 0, 1, 0, tzinfo=datetime.timezone.utc
                    ).timestamp()
                    * 1e9
                ),
                "log_level": "DEBUG",
                "process_name": "wpa",
                "pid": 200,
                "message": "Message 2",
                "source_path": "file2.cc",
                "line_number": 20,
            },
            {
                "timestamp_ns": int(
                    datetime.datetime(
                        2025, 8, 14, 12, 0, 2, 0, tzinfo=datetime.timezone.utc
                    ).timestamp()
                    * 1e9
                ),
                "log_level": "INFO",
                "process_name": "shill",
                "pid": 100,
                "message": "Message 1",  # Duplicate message and source
                "source_path": "file1.cc",
                "line_number": 10,
            },
        ]

    @mock.patch("uuid.uuid4")
    def test_convert_and_save_structure(self, mock_uuid):
        """Tests the basic structure of the generated trace file."""
        # Control the UUIDs returned by the mock
        mock_uuid_values = [
            uuid.UUID(int=1001),  # shill proc
            uuid.UUID(int=1002),  # shill thread
            uuid.UUID(int=2001),  # wpa_proc
            uuid.UUID(int=2002),  # wpa thread
        ]
        mock_uuid.side_effect = mock_uuid_values

        converter = netlog_to_perfetto.PerfettoTraceConverter(
            iter(self.sample_logs)
        )

        with tempfile.NamedTemporaryFile(
            delete=False, suffix=".pftrace"
        ) as tmpfile:
            output_path = tmpfile.name
            converter.convert_and_save(output_path)

        try:
            # Deserialise the generated file
            with open(output_path, "rb") as f:
                trace = perfetto_trace_pb2.Trace.FromString(f.read())

            self.assertTrue(len(trace.packet) > 0)

            # 1. Clock Snapshot
            clock_snapshots = [
                p.clock_snapshot
                for p in trace.packet
                if p.HasField("clock_snapshot")
            ]
            self.assertEqual(
                len(clock_snapshots),
                1,
                msg="Expected exactly one clock snapshot",
            )
            self.assertEqual(
                clock_snapshots[0].clocks[0].clock_id,
                perfetto_trace_pb2.BUILTIN_CLOCK_REALTIME,
            )
            self.assertEqual(
                clock_snapshots[0].clocks[0].timestamp,
                self.sample_logs[0]["timestamp_ns"],
            )

            # 2. Interned Data
            interned_data_packets = [
                p.interned_data
                for p in trace.packet
                if p.HasField("interned_data")
            ]
            self.assertEqual(
                len(interned_data_packets),
                1,
                msg="Expected exactly one interned data packet",
            )
            interned = interned_data_packets[0]

            self.assertEqual(
                len(interned.log_message_body),
                2,
                msg="Expected 2 unique interned log message bodies",
            )
            self.assertEqual(
                len(interned.source_locations),
                2,
                msg="Expected 2 unique interned source locations",
            )

            msg_map = {
                item.iid: item.body for item in interned.log_message_body
            }
            sl_map = {
                item.iid: (item.file_name, item.line_number)
                for item in interned.source_locations
            }

            self.assertIn("Message 1", msg_map.values())
            self.assertIn("Message 2", msg_map.values())
            self.assertIn(("file1.cc", 10), sl_map.values())
            self.assertIn(("file2.cc", 20), sl_map.values())

            # 3. Track Descriptors
            track_descriptors = [
                p.track_descriptor
                for p in trace.packet
                if p.HasField("track_descriptor")
            ]
            self.assertEqual(
                len(track_descriptors),
                4,
                msg="Expected 4 track descriptors: 2 proc * (1 proc+1 thread)",
            )

            proc_descs = {
                td.uuid: td
                for td in track_descriptors
                if td.HasField("process")
            }
            thread_descs = {
                td.uuid: td for td in track_descriptors if td.HasField("thread")
            }

            self.assertIn(
                1001,
                proc_descs,
                msg="Shill process descriptor (UUID 1001) should be present",
            )
            self.assertEqual(proc_descs[1001].process.pid, 100)
            self.assertEqual(proc_descs[1001].process.process_name, "shill")
            self.assertIn(
                1002,
                thread_descs,
                msg="Shill Logs thread descriptor(UUID 1002) should be present",
            )
            self.assertEqual(thread_descs[1002].parent_uuid, 1001)
            self.assertEqual(thread_descs[1002].name, "Logs")

            self.assertIn(
                2001,
                proc_descs,
                msg="WPA process descriptor (UUID 2001) should be present",
            )
            self.assertEqual(proc_descs[2001].process.pid, 200)
            self.assertIn(
                2002,
                thread_descs,
                msg="WPA Logs thread descriptor (UUID 2002) should be present",
            )
            self.assertEqual(thread_descs[2002].parent_uuid, 2001)

            # 4. Track Events (Log Messages)
            track_events = [
                p.track_event for p in trace.packet if p.HasField("track_event")
            ]
            self.assertEqual(len(track_events), 3)

            event0, event1, event2 = track_events

            # Event 0 (shill - Message 1)
            self.assertEqual(
                event0.track_uuid,
                1002,
                msg="Event 0 should be on shill Logs thread (UUID 1002)",
            )
            self.assertEqual(
                event0.type, perfetto_trace_pb2.TrackEvent.TYPE_INSTANT
            )
            self.assertEqual(event0.name, "INFO")
            log_msg0 = event0.log_message
            self.assertEqual(
                log_msg0.prio, netlog_to_perfetto.LOG_PRIORITY_MAP["INFO"]
            )
            self.assertEqual(msg_map[log_msg0.body_iid], "Message 1")
            self.assertEqual(
                sl_map[log_msg0.source_location_iid], ("file1.cc", 10)
            )

            # Event 1 (wpa - Message 2)
            self.assertEqual(
                event1.track_uuid,
                2002,
                msg="Event 1 should be on wpa Logs thread (UUID 2002)",
            )
            self.assertEqual(event1.name, "DEBUG")
            log_msg1 = event1.log_message
            self.assertEqual(
                log_msg1.prio, netlog_to_perfetto.LOG_PRIORITY_MAP["DEBUG"]
            )
            self.assertEqual(msg_map[log_msg1.body_iid], "Message 2")
            self.assertEqual(
                sl_map[log_msg1.source_location_iid], ("file2.cc", 20)
            )

            # Event 2 (shill - Message 1, second time)
            self.assertEqual(
                event2.track_uuid,
                1002,
                msg="Event 2 should be on shill Logs thread (UUID 1002)",
            )
            log_msg2 = event2.log_message
            self.assertEqual(msg_map[log_msg2.body_iid], "Message 1")
            self.assertEqual(
                sl_map[log_msg2.source_location_iid], ("file1.cc", 10)
            )
            self.assertEqual(
                log_msg2.body_iid,
                log_msg0.body_iid,
                msg=(
                    "Event 2 should reuse interned string IID "
                    "from Event 0 for the same message"
                ),
            )
            self.assertEqual(
                log_msg2.source_location_iid,
                log_msg0.source_location_iid,
                msg=(
                    "Event 2 should reuse interned source location IID "
                    "from Event 0"
                ),
            )

        finally:
            os.remove(output_path)


if __name__ == "__main__":
    unittest.main()
