#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Summarizes Sommelier timing information."""

import argparse
from enum import Enum
import statistics
from typing import NamedTuple


_MS_PER_SEC = 1000
_US_PER_SEC = 1000000
# Floating point error bounds
_FP_ERROR = 0.01


class EventType(Enum):
    """Wayland event type."""
    COMMIT = 1
    ATTACH = 2
    RELEASE = 3
    UNKNOWN = 4


class EventInfo(NamedTuple):
    """Stores information of an event."""
    event_type: EventType
    surface_id: int
    buffer_id: int
    time: float


def parse_event_type(event_type):
    EVENT_MAP = {
        'a': EventType.ATTACH,
        'c': EventType.COMMIT,
        'r': EventType.RELEASE,
    }
    return EVENT_MAP.get(event_type, EventType.UNKNOWN)


class FrameLog():
    """Manages access to the Sommelier timing logs."""

    def __init__(self, filename):
        """Parse Sommelier timing log.

        Format of log (header line might be truncated):
        Type Surface_ID Buffer_ID Delta_Time   # header line 1
        a 12 20 4237.44                        # line 2
        ....
        EndTime 3972 1655330324.7              # last line
        Last line format: (EndTime, last event id, time since epoch (s))
        """
        self.frame_log = []
        self.surfaces = set()
        with open(filename, 'r') as f:
            lines = f.read().splitlines()
            total_delta_time = 0
            last_line = lines[-1].split(' ')
            if len(last_line) != 3 or last_line[0] != 'EndTime':
                print(f'Invalid EndTime: {lines[-1]}')
                return
            self.end_time = float(last_line[2])
            for l in reversed(lines[1:-1]):
                line = l.rstrip().split(' ')
                # Skip parsing line that is improperly formatted
                if len(line) != 4:
                    continue
                total_delta_time += float(line[3]) / _US_PER_SEC
                surface_id = int(line[1])
                info = EventInfo(
                    event_type=parse_event_type(line[0]),
                    surface_id=surface_id,
                    buffer_id=int(line[2]),
                    time=self.end_time - total_delta_time
                )
                self.frame_log.append(info)
                self.surfaces.add(surface_id)

    def output_fps(self, windows, ft_target_ms, max_ft_ms):
        """Outputs the summarized fps information based on frame log.

        Args:
            windows: List of time windows (in seconds) to summarize.
            ft_target_ms: Tuple (start, end) ms range of target frame times.
            max_ft_ms: Max frame time threshold (ms).
        """
        for surface in self.surfaces:
            print(f'Summary for surface {surface}')
            print('-------------------------------')
            max_frame_ms = 0
            # only check for commit events on the given surface
            # events are in reverse chronological order
            events = [e for e in self.frame_log if e.surface_id ==
                      surface and e.event_type == EventType.COMMIT]
            if not events:
                print(f'No commit events found for surface {surface}\n')
                continue
            total_sec = self.end_time - events[-1].time
            for w_sec in windows + [total_sec]:
                # num frames in acceptable range
                target_frames = 0
                # num frames exceeding max_ft_ms
                max_ft_events = 0
                prev_sec = self.end_time
                frame_count = 0
                frames_ms = []
                for event in events:
                    frame_ms = (prev_sec - event.time) * _MS_PER_SEC
                    frames_ms.append(frame_ms)
                    max_frame_ms = max(max_frame_ms, frame_ms)
                    if ft_target_ms[0] < frame_ms < ft_target_ms[1]:
                        target_frames += 1
                    if frame_ms > max_ft_ms:
                        max_ft_events += 1
                    current_sec = self.end_time - event.time
                    frame_count += 1
                    if current_sec > w_sec -_FP_ERROR:
                        print(
                            f'FPS (last {w_sec}s): '
                            f'{frame_count / current_sec}')
                        print(
                            f'Max frame time (last {w_sec}s): '
                            f'{max_frame_ms} ms')
                        print(f'Frame count (last {w_sec}s):'
                              f' {frame_count} frames')
                        print(f'Percentage frames within acceptable target '
                              f'{ft_target_ms} ms (last {w_sec}s): '
                              f'{target_frames * 100/frame_count}%')
                        if len(frames_ms) > 1:
                            c_var = (statistics.stdev(
                                frames_ms) / statistics.mean(frames_ms))
                            print(f'Coefficient of variation (last {w_sec}s):'
                                  f' {c_var}')
                        print(f'Frames exceeding max frame time threshold'
                              f' {max_ft_ms} ms (last {w_sec}s):'
                              f' {max_ft_events} frames')
                        print()
                        break
                    prev_sec = event.time
            print()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Return frame summary based on Sommelier timing log.')

    parser.add_argument('file', help='Filename of timing log')
    parser.add_argument('--windows', action='extend', type=int,
                        nargs='+', help='Time windows for summary (in seconds)',
                        default=[10, 60, 300])
    # default values selected based on Battlestar metrics for 60 FPS (16.6 ms
    # frame times), representing a range from 13-19 ms.
    parser.add_argument('--frame-time-target', type=float,
                        nargs=2, help='Frame time targets (in milliseconds)',
                        default=(13, 19))
    parser.add_argument('--max-frame-time', type=float,
                        help='Max frame time threshold (in milliseconds)',
                        default=200)
    args = parser.parse_args()
    log = FrameLog(args.file)
    log.output_fps(windows=sorted(args.windows),
                   ft_target_ms=args.frame_time_target,
                   max_ft_ms=args.max_frame_time)
