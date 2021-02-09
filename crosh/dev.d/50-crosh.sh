#!/bin/dash
# Copyright (c) 2009-2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# dev-mode functionality for crosh

USAGE_systrace='[<start | stop | status>]'
HELP_systrace='
  Start/stop system tracing.  Turning tracing off will generate a trace
  log file in the Downloads directory with all the events collected
  since the last time tracing was enabled.  One can control the events
  collected by specifying categories after "start"; e.g. "start gfx"
  will collect only graphics-related system events.  "systrace status"
  (or just "systrace") will display the current state of tracing, including
  the set of events being traced.
'
cmd_systrace() (
  case x"$1" in
  xstart)
    local categories;
    shift; categories="$*"
    if [ -z "${categories}" ]; then
       categories="all"
    fi
    debugd SystraceStart "string:${categories}"
    ;;
  xstop)
    local downloads_dir="/home/${USER}/user/Downloads"
    local data_file="$(mktemp "${downloads_dir}/systrace.XXXXXX")"
    if [ $? -ne 0 ]; then
      echo "Cannot create data file ${data_file}"
      return 1
    fi
    debugd SystraceStop "fd:1" > "${data_file}"
    echo "Trace data saved to ${data_file}"
    # add symlink to the latest capture file
    ln -sf "$(basename "${data_file}")" "${downloads_dir}/systrace.latest"
    ;;
  xstatus|x)
    debugd SystraceStatus
    ;;
  esac
)

USAGE_live_in_a_coal_mine=''
HELP_live_in_a_coal_mine='
  Switch to the canary channel.

  WARNING: This is bleeding edge software and is often more buggy than the dev
  channel.  Please do not use this unless you are a developer.  This is often
  updated daily and has only passed automated tests -- the QA level is low!

  This channel may not always boot reliably or have a functioning auto update
  mechanism. Do not do this unless you are prepared to recover your Chrome OS
  device, please be familiar with this article first:
  https://support.google.com/chromebook/answer/1080595
'
cmd_live_in_a_coal_mine() (
  shell_read "Are you sure you want to change to the canary channel? [y/N] "
  case "${REPLY}" in
  [yY])
    /usr/bin/update_engine_client -channel=canary-channel
    /usr/bin/update_engine_client --show_channel
    ;;
  *) echo "Fly, my pretties, fly! (not changing channels)";;
  esac
)
