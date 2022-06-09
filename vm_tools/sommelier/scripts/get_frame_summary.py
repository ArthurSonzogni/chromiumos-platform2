#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Summarizes Sommelier timing information."""

import argparse
from enum import Enum
from typing import NamedTuple


_MS_IN_SEC = 1000
_US_IN_SEC = 1000000


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
                total_delta_time += float(line[3]) / _US_IN_SEC
                surface_id = int(line[1])
                info = EventInfo(
                    event_type=parse_event_type(line[0]),
                    surface_id=surface_id,
                    buffer_id=int(line[2]),
                    time=self.end_time - total_delta_time
                )
                self.frame_log.append(info)
                self.surfaces.add(surface_id)

    def output_fps(self, windows):
        """Outputs the summarized fps information based on frame log.

        Args:
            windows: List of time windows (in seconds) to summarize.
        """
        for surface in self.surfaces:
            print(f'Summary for surface {surface}')
            max_frame_time = 0
            win = windows[:]
            # only check for commit events on the given surface
            # events are in reverse chronological order
            events = [e for e in self.frame_log if e.surface_id ==
                      surface and e.event_type == EventType.COMMIT]
            if not events:
                print(f'No commit events found for surface {surface}\n')
                continue
            prev_time = self.end_time
            for i, event in enumerate(events):
                max_frame_time = max(
                    max_frame_time, prev_time - event.time)
                for w in win:
                    if self.end_time - event.time > w:
                        print(
                            f'FPS (last {w}s): '
                            f'{(i+1) / (self.end_time - event.time)}')
                        print(
                            f'Max frame time (last {w}s): '
                            f'{max_frame_time * _MS_IN_SEC}')
                        print(f'Frame count (last {w}s): {i+1}')
                win = [w for w in win if self.end_time - event.time <= w]
                prev_time = event.time
            print(
                f'FPS (all time): '
                f'{len(events) / (self.end_time - events[-1].time)}')
            print(
                f'Max frame time (all time): {max_frame_time * _MS_IN_SEC}')
            print(f'Total frame count: {len(events)}\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Return frame summary based on Sommelier timing log.')

    parser.add_argument('file', help='Filename of timing log')
    parser.add_argument('--windows', action='extend', type=int,
                        nargs='+', help='Time windows for summary (in seconds)',
                        default=[10, 60, 300])
    args = parser.parse_args()
    log = FrameLog(args.file)
    log.output_fps(windows=sorted(args.windows))
