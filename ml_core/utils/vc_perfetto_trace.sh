#!/bin/bash

# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use this file to generate a perfetto trace file for analysis.
# Includes Intel GPU counters

perfetto \
  -c - --txt \
  -o /tmp/trace \
<<EOF

buffers: {
    size_kb: 260096
    fill_policy: DISCARD
}
buffers: {
    size_kb: 2048
    fill_policy: DISCARD
}
data_sources: {
    config {
        name: "android.gpu.memory"
    }
}
data_sources: {
    config {
        name: "android.power"
        android_power_config {
            battery_poll_ms: 1000
            battery_counters: BATTERY_COUNTER_CAPACITY_PERCENT
            battery_counters: BATTERY_COUNTER_CHARGE
            battery_counters: BATTERY_COUNTER_CURRENT
            collect_power_rails: true
        }
    }
}
data_sources: {
    config {
        name: "linux.process_stats"
        target_buffer: 1
        process_stats_config {
            scan_all_processes_on_start: true
            proc_stats_poll_ms: 1000
        }
    }
}
data_sources: {
    config {
        name: "android.log"
        android_log_config {
        }
    }
}
data_sources: {
    config {
        name: "org.chromium.trace_event"
        chrome_config {
            trace_config: "{\"record_mode\":\"record-until-full\",\"included_categories\":[\"accessibility\",\"AccountFetcherService\",\"android_webview\",\"aogh\",\"audio\",\"base\",\"benchmark\",\"blink\",\"blink.animations\",\"blink.bindings\",\"blink.console\",\"blink.net\",\"blink.resource\",\"blink.user_timing\",\"blink.worker\",\"blink_gc\",\"blink_style\",\"Blob\",\"browser\",\"browsing_data\",\"CacheStorage\",\"Calculators\",\"CameraStream\",\"camera\",\"cast_app\",\"cast_perf_test\",\"cast.mdns\",\"cast.mdns.socket\",\"cast.stream\",\"cc\",\"cc.debug\",\"cdp.perf\",\"chromeos\",\"cma\",\"compositor\",\"content\",\"content_capture\",\"device\",\"devtools\",\"devtools.contrast\",\"devtools.timeline\",\"disk_cache\",\"download\",\"download_service\",\"drm\",\"drmcursor\",\"dwrite\",\"DXVA_Decoding\",\"evdev\",\"event\",\"exo\",\"extensions\",\"explore_sites\",\"FileSystem\",\"file_system_provider\",\"fonts\",\"GAMEPAD\",\"gpu\",\"gpu.angle\",\"gpu.capture\",\"headless\",\"hwoverlays\",\"identity\",\"ime\",\"IndexedDB\",\"input\",\"io\",\"ipc\",\"Java\",\"jni\",\"jpeg\",\"latency\",\"latencyInfo\",\"leveldb\",\"loading\",\"log\",\"login\",\"media\",\"media_router\",\"memory\",\"midi\",\"mojom\",\"mus\",\"native\",\"navigation\",\"net\",\"netlog\",\"offline_pages\",\"omnibox\",\"oobe\",\"ozone\",\"partition_alloc\",\"passwords\",\"p2p\",\"page-serialization\",\"paint_preview\",\"pepper\",\"PlatformMalloc\",\"power\",\"ppapi\",\"ppapi_proxy\",\"print\",\"rail\",\"renderer\",\"renderer_host\",\"renderer.scheduler\",\"RLZ\",\"safe_browsing\",\"screenlock_monitor\",\"segmentation_platform\",\"sequence_manager\",\"service_manager\",\"ServiceWorker\",\"sharing\",\"shell\",\"shortcut_viewer\",\"shutdown\",\"SiteEngagement\",\"skia\",\"sql\",\"stadia_media\",\"stadia_rtc\",\"startup\",\"sync\",\"system_apps\",\"test_gpu\",\"thread_pool\",\"toplevel\",\"toplevel.flow\",\"ui\",\"v8\",\"v8.execute\",\"v8.wasm\",\"ValueStoreFrontend::Backend\",\"views\",\"views.frame\",\"viz\",\"vk\",\"wayland\",\"webaudio\",\"weblayer\",\"WebCore\",\"webrtc\",\"xr\"],\"memory_dump_config\":{}}"
        }
    }
}
data_sources: {
    config {
        name: "org.chromium.trace_metadata"
        chrome_config {
            trace_config: "{\"record_mode\":\"record-until-full\",\"included_categories\":[\"accessibility\",\"AccountFetcherService\",\"android_webview\",\"aogh\",\"audio\",\"base\",\"benchmark\",\"blink\",\"blink.animations\",\"blink.bindings\",\"blink.console\",\"blink.net\",\"blink.resource\",\"blink.user_timing\",\"blink.worker\",\"blink_gc\",\"blink_style\",\"Blob\",\"browser\",\"browsing_data\",\"CacheStorage\",\"Calculators\",\"CameraStream\",\"camera\",\"cast_app\",\"cast_perf_test\",\"cast.mdns\",\"cast.mdns.socket\",\"cast.stream\",\"cc\",\"cc.debug\",\"cdp.perf\",\"chromeos\",\"cma\",\"compositor\",\"content\",\"content_capture\",\"device\",\"devtools\",\"devtools.contrast\",\"devtools.timeline\",\"disk_cache\",\"download\",\"download_service\",\"drm\",\"drmcursor\",\"dwrite\",\"DXVA_Decoding\",\"evdev\",\"event\",\"exo\",\"extensions\",\"explore_sites\",\"FileSystem\",\"file_system_provider\",\"fonts\",\"GAMEPAD\",\"gpu\",\"gpu.angle\",\"gpu.capture\",\"headless\",\"hwoverlays\",\"identity\",\"ime\",\"IndexedDB\",\"input\",\"io\",\"ipc\",\"Java\",\"jni\",\"jpeg\",\"latency\",\"latencyInfo\",\"leveldb\",\"loading\",\"log\",\"login\",\"media\",\"media_router\",\"memory\",\"midi\",\"mojom\",\"mus\",\"native\",\"navigation\",\"net\",\"netlog\",\"offline_pages\",\"omnibox\",\"oobe\",\"ozone\",\"partition_alloc\",\"passwords\",\"p2p\",\"page-serialization\",\"paint_preview\",\"pepper\",\"PlatformMalloc\",\"power\",\"ppapi\",\"ppapi_proxy\",\"print\",\"rail\",\"renderer\",\"renderer_host\",\"renderer.scheduler\",\"RLZ\",\"safe_browsing\",\"screenlock_monitor\",\"segmentation_platform\",\"sequence_manager\",\"service_manager\",\"ServiceWorker\",\"sharing\",\"shell\",\"shortcut_viewer\",\"shutdown\",\"SiteEngagement\",\"skia\",\"sql\",\"stadia_media\",\"stadia_rtc\",\"startup\",\"sync\",\"system_apps\",\"test_gpu\",\"thread_pool\",\"toplevel\",\"toplevel.flow\",\"ui\",\"v8\",\"v8.execute\",\"v8.wasm\",\"ValueStoreFrontend::Backend\",\"views\",\"views.frame\",\"viz\",\"vk\",\"wayland\",\"webaudio\",\"weblayer\",\"WebCore\",\"webrtc\",\"xr\"],\"memory_dump_config\":{}}"
        }
    }
}
data_sources: {
    config {
        name: "linux.sys_stats"
        sys_stats_config {
            stat_period_ms: 1000
            stat_counters: STAT_CPU_TIMES
            stat_counters: STAT_FORK_COUNT
        }
    }
}
data_sources: {
    config {
        name: "linux.ftrace"
        ftrace_config {
            ftrace_events: "sched/sched_switch"
            ftrace_events: "power/suspend_resume"
            ftrace_events: "sched/sched_wakeup"
            ftrace_events: "sched/sched_wakeup_new"
            ftrace_events: "sched/sched_waking"
            ftrace_events: "power/cpu_frequency"
            ftrace_events: "power/cpu_idle"
            ftrace_events: "power/gpu_frequency"
            ftrace_events: "gpu_mem/gpu_mem_total"
            ftrace_events: "regulator/regulator_set_voltage"
            ftrace_events: "regulator/regulator_set_voltage_complete"
            ftrace_events: "power/clock_enable"
            ftrace_events: "power/clock_disable"
            ftrace_events: "power/clock_set_rate"
            ftrace_events: "sched/sched_process_exit"
            ftrace_events: "sched/sched_process_free"
            ftrace_events: "task/task_newtask"
            ftrace_events: "task/task_rename"
            ftrace_events: "ftrace/print"
            atrace_apps: "*"
            buffer_size_kb: 2048
            drain_period_ms: 250
        }
    }
}
data_sources {
    config {
        name: "track_event"
        target_buffer: 0
        track_event_config {
            disabled_categories: "*"
            enabled_categories: "accessibility"
            enabled_categories: "AccountFetcherService"
            enabled_categories: "android_webview"
            enabled_categories: "aogh"
            enabled_categories: "audio"
            enabled_categories: "base"
            enabled_categories: "benchmark"
            enabled_categories: "blink"
            enabled_categories: "blink.animations"
            enabled_categories: "blink.bindings"
            enabled_categories: "blink.console"
            enabled_categories: "blink.net"
            enabled_categories: "blink.resource"
            enabled_categories: "blink.user_timing"
            enabled_categories: "blink.worker"
            enabled_categories: "blink_gc"
            enabled_categories: "blink_style"
            enabled_categories: "Blob"
            enabled_categories: "browser"
            enabled_categories: "browsing_data"
            enabled_categories: "CacheStorage"
            enabled_categories: "Calculators"
            enabled_categories: "CameraStream"
            enabled_categories: "camera"
            enabled_categories: "cast_app"
            enabled_categories: "cast_perf_test"
            enabled_categories: "cast.mdns"
            enabled_categories: "cast.mdns.socket"
            enabled_categories: "cast.stream"
            enabled_categories: "cc"
            enabled_categories: "cc.debug"
            enabled_categories: "cdp.perf"
            enabled_categories: "chromeos"
            enabled_categories: "cma"
            enabled_categories: "compositor"
            enabled_categories: "content"
            enabled_categories: "content_capture"
            enabled_categories: "device"
            enabled_categories: "devtools"
            enabled_categories: "devtools.contrast"
            enabled_categories: "devtools.timeline"
            enabled_categories: "disk_cache"
            enabled_categories: "download"
            enabled_categories: "download_service"
            enabled_categories: "drm"
            enabled_categories: "drmcursor"
            enabled_categories: "dwrite"
            enabled_categories: "DXVA_Decoding"
            enabled_categories: "evdev"
            enabled_categories: "event"
            enabled_categories: "exo"
            enabled_categories: "extensions"
            enabled_categories: "explore_sites"
            enabled_categories: "FileSystem"
            enabled_categories: "file_system_provider"
            enabled_categories: "fonts"
            enabled_categories: "GAMEPAD"
            enabled_categories: "gpu"
            enabled_categories: "gpu.angle"
            enabled_categories: "gpu.capture"
            enabled_categories: "headless"
            enabled_categories: "hwoverlays"
            enabled_categories: "identity"
            enabled_categories: "ime"
            enabled_categories: "IndexedDB"
            enabled_categories: "input"
            enabled_categories: "io"
            enabled_categories: "ipc"
            enabled_categories: "Java"
            enabled_categories: "jni"
            enabled_categories: "jpeg"
            enabled_categories: "latency"
            enabled_categories: "latencyInfo"
            enabled_categories: "leveldb"
            enabled_categories: "loading"
            enabled_categories: "log"
            enabled_categories: "login"
            enabled_categories: "media"
            enabled_categories: "media_router"
            enabled_categories: "memory"
            enabled_categories: "midi"
            enabled_categories: "mojom"
            enabled_categories: "mus"
            enabled_categories: "native"
            enabled_categories: "navigation"
            enabled_categories: "net"
            enabled_categories: "netlog"
            enabled_categories: "offline_pages"
            enabled_categories: "omnibox"
            enabled_categories: "oobe"
            enabled_categories: "ozone"
            enabled_categories: "partition_alloc"
            enabled_categories: "passwords"
            enabled_categories: "p2p"
            enabled_categories: "page-serialization"
            enabled_categories: "paint_preview"
            enabled_categories: "pepper"
            enabled_categories: "PlatformMalloc"
            enabled_categories: "power"
            enabled_categories: "ppapi"
            enabled_categories: "ppapi_proxy"
            enabled_categories: "print"
            enabled_categories: "rail"
            enabled_categories: "renderer"
            enabled_categories: "renderer_host"
            enabled_categories: "renderer.scheduler"
            enabled_categories: "RLZ"
            enabled_categories: "safe_browsing"
            enabled_categories: "screenlock_monitor"
            enabled_categories: "segmentation_platform"
            enabled_categories: "sequence_manager"
            enabled_categories: "service_manager"
            enabled_categories: "ServiceWorker"
            enabled_categories: "sharing"
            enabled_categories: "shell"
            enabled_categories: "shortcut_viewer"
            enabled_categories: "shutdown"
            enabled_categories: "SiteEngagement"
            enabled_categories: "skia"
            enabled_categories: "sql"
            enabled_categories: "stadia_media"
            enabled_categories: "stadia_rtc"
            enabled_categories: "startup"
            enabled_categories: "sync"
            enabled_categories: "system_apps"
            enabled_categories: "test_gpu"
            enabled_categories: "thread_pool"
            enabled_categories: "toplevel"
            enabled_categories: "toplevel.flow"
            enabled_categories: "ui"
            enabled_categories: "v8"
            enabled_categories: "v8.execute"
            enabled_categories: "v8.wasm"
            enabled_categories: "ValueStoreFrontend::Backend"
            enabled_categories: "views"
            enabled_categories: "views.frame"
            enabled_categories: "viz"
            enabled_categories: "vk"
            enabled_categories: "wayland"
            enabled_categories: "webaudio"
            enabled_categories: "weblayer"
            enabled_categories: "WebCore"
            enabled_categories: "webrtc"
            enabled_categories: "xr"
            enabled_categories: "__metadata"
            timestamp_unit_multiplier: 1000
            filter_debug_annotations: false
            enable_thread_time_sampling: true
            filter_dynamic_event_names: false
        }
    }
    producer_name_regex_filter: "org.chromium-.*"
}
data_sources {
    config {
        name: "track_event"
        target_buffer: 0
    }
    producer_name_regex_filter: "cros_camera.*"
}
data_sources {
    config {
        name: "gpu.counters.i915"
        target_buffer: 0
    }
}
data_sources {
    config {
        name: "gpu.renderstages.intel"
        target_buffer: 0
    }
}
duration_ms: 10000

EOF
