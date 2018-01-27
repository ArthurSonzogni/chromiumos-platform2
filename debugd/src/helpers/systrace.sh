#! /bin/sh

# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# Helper script for turning system event tracing on/off.
#
# systrace start [ event-category ... ]
# systrace stop
# systrace [status]
#
# Event categories are one or more of "sched" (thread switch events),
# "workq" (workq start/stop events), "gfx" (select i915 events),
# and "power" (cpu frequency change events).  "all" can be used
# to enable all supported events.  If no categories are specified
# then "all" are enabled.
#
# While collecting events the trace ring-buffer size is increased
# based on the set of enabled events.
#
# On disabling tracing collected events are written to standard out
# which is expected to be directed to a file or similar.
#
set -e

CMD=${1:-status}

tracing_path=/sys/kernel/debug/tracing
# TODO(sleffler) enable more/different events
sched_events="
    sched:sched_switch
    sched:sched_wakeup
"
workq_events="
    workqueue:workqueue_execute_start
    workqueue:workqueue_execute_end
"
gfx_events="
    i915:i915_flip_request
    i915:i915_flip_complete
    i915:i915_gem_object_pwrite
    i915:intel_gpu_freq_change
    exynos:exynos_flip_request
    exynos:exynos_flip_complete
    exynos:exynos_page_flip_state
    drm:drm_vblank_event
"
power_events="
    power:cpu_idle
    power:cpu_frequency
    mali:mali_dvfs_set_clock
    mali:mali_dvfs_set_voltage
    cpufreq_interactive:cpufreq_interactive_boost
    cpufreq_interactive:cpufreq_interactive_unboost
    exynos_busfreq:exynos_busfreq_target_int
    exynos_busfreq:exynos_busfreq_target_mif
"

input_events="
    irq:irq_threaded_handler_entry
    irq:irq_threaded_handler_exit
"

# TODO(sleffler) calculate based on enabled events
buffer_size_running=7040           # ring-buffer size in kb / cpu
buffer_size_idle=1408              # ring-buffer size while idle

if test ! -e ${tracing_path}; then
    echo "Kernel tracing not available (missing ${tracing_path})" >&2
    exit
fi

tracing_write()
{
    echo $1 > $tracing_path/$2
}

tracing_enable()
{
    tracing_write 1 tracing_on
}

tracing_disable()
{
    tracing_write 0 tracing_on
}

tracing_enable_events()
{
    local events_enabled
    local events_failed
    for ev; do
        # NB: note >>
        if echo ${ev} >> ${tracing_path}/set_event; then
            events_enabled="${events_enabled} ${ev}"
        else
            events_failed="${events_failed} ${ev}"
        fi
    done
    logger -t systrace "enable events ${events_enabled}"
    if [ -n "${events_failed}" ]; then
        logger -t systrace "Warning, events ${events_failed} were not enabled"
    fi
}

tracing_reset()
{
    tracing_disable                             # stop kernel tracing
    tracing_write "" set_event                  # clear enabled events
}

parse_category()
{
    case $1 in
    gfx)    echo "${gfx_events}";;
    input)  echo "${input_events}";;
    power)  echo "${power_events}";;
    sched)  echo "${sched_events}";;
    workq)  echo "${workq_events}";;

    all)    echo "${sched_events}
                  ${workq_events}
                  ${power_events}
                  ${gfx_events}";;
    *)
      echo "Unknown category \'${category}\'" >&2
      exit
      ;;
    esac
}

is_enabled=$(cat ${tracing_path}/tracing_on)

case "$CMD" in
start)
    events=""
    shift
    for cat; do
        events="${events} $(parse_category $cat)"
    done
    if [ -z "${events}" ]; then
        events=$(parse_category 'all')
    fi

    if [ "$is_enabled" = "1" ]; then
        tracing_reset
    fi

    logger -t systrace "start tracing"
    tracing_write "mono" trace_clock          # monotonic clock for timestamps
    tracing_enable_events ${events}
    tracing_write "${buffer_size_running}" buffer_size_kb
    tracing_enable                              # start kernel tracing
    ;;
stop)
    if [ "$is_enabled" = "0" ]; then
        echo "Tracing is not enabled; nothing to do" >&2
        exit
    fi

    logger -t systrace "stop tracing"
    # Add null sync marker for chrome so events are 0-time-shifted
    # (on chrome os user-space events are stamped with the kernel
    #  trace clock so there is no need to time-shift events).
    tracing_write 'trace_event_clock_sync: parent_ts=0' trace_marker
    tracing_reset

    # NB: debugd attaches stdout to an fd passed in by the client
    cat ${tracing_path}/trace
    tracing_write "0" trace                     # clear trace buffer
    tracing_write "${buffer_size_idle}" buffer_size_kb
    ;;
status)
    echo -n 'enabled: '; cat ${tracing_path}/tracing_on
    echo -n 'clock:   '; cat ${tracing_path}/trace_clock
    echo 'enabled events:'; cat ${tracing_path}/set_event | sed 's/^/   /'
    ;;
*)
    echo "Unknown request ${CMD}; use one of start, stop, status" >&2
    exit 1
esac

exit 0
