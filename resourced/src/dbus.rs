// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::convert::TryFrom;
use std::path::Path;
use std::path::PathBuf;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use dbus::channel::MatchingReceiver;
use dbus::channel::Sender;
use dbus::message::MatchRule;
use dbus::message::Message;
use dbus::nonblock::Proxy;
use dbus::nonblock::SyncConnection;
use dbus_crossroads::Crossroads;
use dbus_crossroads::IfaceBuilder;
use dbus_crossroads::IfaceToken;
use dbus_crossroads::MethodErr;
use dbus_tokio::connection;
use log::error;
use log::LevelFilter;
use system_api::battery_saver::BatterySaverModeState;

use crate::common;
use crate::config;
use crate::feature;
use crate::memory;
use crate::power;
use crate::proc::load_euid;
use crate::psi;
use crate::qos;
use crate::qos::set_process_state;
use crate::qos::set_thread_state;
use crate::qos::SchedQosContext;
use crate::vm_memory_management_client::VmMemoryManagementClient;

const SERVICE_NAME: &str = "org.chromium.ResourceManager";
const PATH_NAME: &str = "/org/chromium/ResourceManager";
const INTERFACE_NAME: &str = SERVICE_NAME;

const VMCONCIEGE_INTERFACE_NAME: &str = "org.chromium.VmConcierge";
const POWERD_INTERFACE_NAME: &str = "org.chromium.PowerManager";
const POWERD_PATH_NAME: &str = "/org/chromium/PowerManager";

pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(5);

// The timeout in second for VM boot mode. Currently this is
// 60seconds which is long enough for booting a VM on low-end DUTs.
const DEFAULT_VM_BOOT_TIMEOUT: Duration = Duration::from_secs(60);

const VARIABLE_TIME_MEMORY_SIGNAL_FEATURE_NAME: &str =
    "CrOSLateBootResourcedVariableTimeMemorySignal";

type PowerPreferencesManager = power::DirectoryPowerPreferencesManager<
    config::DirectoryConfigProvider,
    power::DirectoryPowerSourceProvider,
>;

// Context data for the D-Bus service.
#[derive(Clone)]
struct DbusContext {
    power_preferences_manager: Arc<PowerPreferencesManager>,

    // Timer ids for skipping out-of-dated timer events.
    reset_game_mode_timer_id: Arc<AtomicUsize>,
    reset_fullscreen_video_timer_id: Arc<AtomicUsize>,
    reset_vm_boot_mode_timer_id: Arc<AtomicUsize>,

    scheduler_context: Option<Arc<Mutex<SchedQosContext>>>,
}

fn send_pressure_signal(
    conn: &SyncConnection,
    signal_name: &str,
    level: u8,
    reclaim_target_kb: u64,
) {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };

    let mut signal_origin_timestamp_ms = -1;

    // SAFETY: Safe because the only side-effects of this function are modifications via the
    // passed pointer, and we pass a pointer of the proper type.
    let result =
        unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts as *mut libc::timespec) };
    if result == 0 {
        signal_origin_timestamp_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1_000_000;
    } else {
        error!("Failed to get current time.");
    }

    let msg = Message::signal(
        &PATH_NAME.into(),
        &INTERFACE_NAME.into(),
        &signal_name.into(),
    )
    // Since the origin timestamp originates from CLOCK_MONOTONIC,
    // it should not be used from any guest context. It can only be compared within the host.
    .append3(level, reclaim_target_kb, signal_origin_timestamp_ms);
    if conn.send(msg).is_err() {
        error!("Send Chrome pressure signal failed.");
    }
}

// Call swap_management SwapSetSwappiness when set_game_mode returns TuneSwappiness.
fn set_game_mode_and_tune_swappiness(
    power_preferences_manager: &dyn power::PowerPreferencesManager,
    mode: common::GameMode,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    if let Some(common::TuneSwappiness { swappiness }) =
        common::set_game_mode(power_preferences_manager, mode, PathBuf::from("/"))?
    {
        const SWAPPINESS_PATH: &str = "/proc/sys/vm/swappiness";
        if swappiness != common::read_file_to_u64(SWAPPINESS_PATH)? as u32 {
            tokio::spawn(async move {
                let swap_management_proxy = Proxy::new(
                    "org.chromium.SwapManagement",
                    "/org/chromium/SwapManagement",
                    DEFAULT_DBUS_TIMEOUT,
                    conn,
                );
                match swap_management_proxy
                    .method_call(
                        "org.chromium.SwapManagement",
                        "SwapSetSwappiness",
                        (swappiness,),
                    )
                    .await
                {
                    Ok(()) => (), // For type inference.
                    Err(e) => error!("Calling SwapSetSwappiness failed: {:#}", e),
                }
            });
        }
    }
    Ok(())
}

async fn get_sender_euid(conn: Arc<SyncConnection>, bus_name: Option<String>) -> Result<u32> {
    let Some(bus_name) = bus_name else {
        bail!("sender bus name is empty");
    };

    let (sender_pid,): (u32,) = Proxy::new(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        DEFAULT_DBUS_TIMEOUT,
        conn,
    )
    .method_call(
        "org.freedesktop.DBus",
        "GetConnectionUnixProcessID",
        (bus_name,),
    )
    .await
    .context("get sender pid")?;

    load_euid(sender_pid).context("load euid")
}

fn register_interface(cr: &mut Crossroads, conn: Arc<SyncConnection>) -> IfaceToken<DbusContext> {
    cr.register(INTERFACE_NAME, |b: &mut IfaceBuilder<DbusContext>| {
        b.method(
            "GetAvailableMemoryKB",
            (),
            ("available",),
            move |_, _, ()| {
                let game_mode = match common::get_game_mode() {
                    Ok(available) => Ok(available),
                    Err(_) => Err(MethodErr::failed("Couldn't get game mode state")),
                }?;
                match memory::get_background_available_memory_kb(game_mode) {
                    Ok(available) => Ok((available,)),
                    Err(_) => Err(MethodErr::failed("Couldn't get available memory")),
                }
            },
        );
        b.method(
            "GetForegroundAvailableMemoryKB",
            (),
            ("available",),
            move |_, _, ()| match memory::get_foreground_available_memory_kb() {
                Ok(available) => Ok((available,)),
                Err(_) => Err(MethodErr::failed(
                    "Couldn't get foreground available memory",
                )),
            },
        );
        b.method(
            "GetMemoryMarginsKB",
            (),
            ("critical", "moderate"),
            move |_, _, ()| {
                let margins = memory::get_memory_margins_kb();
                Ok((margins.0, margins.1))
            },
        );
        b.method(
            "GetComponentMemoryMarginsKB",
            (),
            ("margins",),
            move |_, _, ()| {
                let margins = memory::get_component_margins_kb();
                let result = HashMap::from([
                    ("ChromeCritical", margins.chrome_critical),
                    ("ChromeModerate", margins.chrome_moderate),
                    ("ArcvmForeground", margins.arcvm.foreground),
                    ("ArcvmPerceptible", margins.arcvm.perceptible),
                    ("ArcvmCached", margins.arcvm.cached),
                    ("ArcContainerForeground", margins.arc_container.foreground),
                    ("ArcContainerPerceptible", margins.arc_container.perceptible),
                    ("ArcContainerCached", margins.arc_container.cached),
                ]);
                Ok((result,))
            },
        );
        b.method(
            "SetMemoryMarginsBps",
            ("critical_bps", "moderate_bps"),
            ("critical", "moderate"),
            move |_, _, (critical_bps, moderate_bps): (u32, u32)| {
                match memory::set_memory_margins_bps(critical_bps, moderate_bps) {
                    Ok(()) => {
                        let margins = memory::get_memory_margins_kb();
                        Ok((margins.0, margins.1))
                    }
                    Err(_) => Err(MethodErr::failed("Failed to set memory thresholds")),
                }
            },
        );
        b.method(
            "GetGameMode",
            (),
            ("game_mode",),
            move |_, _, ()| match common::get_game_mode() {
                Ok(game_mode) => Ok((game_mode as u8,)),
                Err(_) => Err(MethodErr::failed("Failed to get game mode")),
            },
        );
        let conn_clone = conn.clone();
        b.method(
            "SetGameMode",
            ("game_mode",),
            (),
            move |_, context, (mode_raw,): (u8,)| {
                let mode = common::GameMode::try_from(mode_raw)
                    .map_err(|_| MethodErr::failed("Unsupported game mode value"))?;

                set_game_mode_and_tune_swappiness(
                    context.power_preferences_manager.as_ref(),
                    mode,
                    conn_clone.clone(),
                )
                .map_err(|e| {
                    error!("set_game_mode failed: {:#}", e);

                    MethodErr::failed("Failed to set game mode")
                })?;
                context
                    .reset_game_mode_timer_id
                    .fetch_add(1, Ordering::Relaxed);
                Ok(())
            },
        );
        let conn_clone2 = conn.clone();
        b.method(
            "SetGameModeWithTimeout",
            ("game_mode", "timeout_sec"),
            ("origin_game_mode",),
            move |_, context, (mode_raw, timeout_raw): (u8, u32)| {
                let old_game_mode = common::get_game_mode()
                    .map_err(|_| MethodErr::failed("Failed to get game mode"))?;
                let mode = common::GameMode::try_from(mode_raw)
                    .map_err(|_| MethodErr::failed("Unsupported game mode value"))?;
                let timeout = Duration::from_secs(timeout_raw.into());
                set_game_mode_and_tune_swappiness(
                    context.power_preferences_manager.as_ref(),
                    mode,
                    conn_clone2.clone(),
                )
                .map_err(|e| {
                    error!("set_game_mode failed: {:#}", e);

                    MethodErr::failed("Failed to set game mode")
                })?;

                // Increase timer id to cancel previous timer events.
                context
                    .reset_game_mode_timer_id
                    .fetch_add(1, Ordering::Relaxed);
                let timer_id = context.reset_game_mode_timer_id.load(Ordering::Relaxed);
                let power_preferences_manager = context.power_preferences_manager.clone();
                let reset_game_mode_timer_id = context.reset_game_mode_timer_id.clone();

                // Reset game mode after timeout.
                let conn_clone = conn_clone2.clone();
                tokio::spawn(async move {
                    tokio::time::sleep(timeout).await;
                    // If the timer id is changed, this event is canceled.
                    if timer_id == reset_game_mode_timer_id.load(Ordering::Relaxed)
                        && set_game_mode_and_tune_swappiness(
                            power_preferences_manager.as_ref(),
                            common::GameMode::Off,
                            conn_clone,
                        )
                        .is_err()
                    {
                        error!("Reset game mode failed.");
                    }
                });

                Ok((old_game_mode as u8,))
            },
        );
        b.method("GetRTCAudioActive", (), ("mode",), move |_, _, ()| {
            match common::get_rtc_audio_active() {
                Ok(active) => Ok((active as u8,)),
                Err(_) => Err(MethodErr::failed("Failed to get RTC audio activity")),
            }
        });
        b.method(
            "SetRTCAudioActive",
            ("mode",),
            (),
            move |_, context, (active_raw,): (u8,)| {
                let active = common::RTCAudioActive::try_from(active_raw)
                    .map_err(|_| MethodErr::failed("Unsupported RTC audio active value"))?;
                match common::set_rtc_audio_active(
                    context.power_preferences_manager.as_ref(),
                    active,
                ) {
                    Ok(()) => Ok(()),
                    Err(e) => {
                        error!("set_rtc_audeio_active failed: {:#}", e);
                        Err(MethodErr::failed("Failed to set RTC audio activity"))
                    }
                }
            },
        );
        b.method("GetFullscreenVideo", (), ("mode",), move |_, _, ()| {
            match common::get_fullscreen_video() {
                Ok(mode) => Ok((mode as u8,)),
                Err(_) => Err(MethodErr::failed("Failed to get fullscreen video activity")),
            }
        });
        b.method(
            "SetFullscreenVideoWithTimeout",
            ("mode", "timeout_sec"),
            (),
            |_, context, (mode_raw, timeout_raw): (u8, u32)| {
                let mode = common::FullscreenVideo::try_from(mode_raw)
                    .map_err(|_| MethodErr::failed("Unsupported fullscreen video value"))?;
                let timeout = Duration::from_secs(timeout_raw.into());

                common::set_fullscreen_video(context.power_preferences_manager.as_ref(), mode)
                    .map_err(|e| {
                        error!("set_fullscreen_video failed: {:#}", e);

                        MethodErr::failed("Failed to set full screen video mode")
                    })?;

                context
                    .reset_fullscreen_video_timer_id
                    .fetch_add(1, Ordering::Relaxed);
                let timer_id = context
                    .reset_fullscreen_video_timer_id
                    .load(Ordering::Relaxed);
                let power_preferences_manager = context.power_preferences_manager.clone();
                let reset_fullscreen_video_timer_id =
                    context.reset_fullscreen_video_timer_id.clone();
                tokio::spawn(async move {
                    tokio::time::sleep(timeout).await;
                    if timer_id == reset_fullscreen_video_timer_id.load(Ordering::Relaxed)
                        && common::set_fullscreen_video(
                            power_preferences_manager.as_ref(),
                            common::FullscreenVideo::Inactive,
                        )
                        .is_err()
                    {
                        error!("Reset fullscreen video mode failed.");
                    }
                });

                Ok(())
            },
        );
        b.method("PowerSupplyChange", (), (), move |_, context, ()| {
            match common::update_power_preferences(context.power_preferences_manager.as_ref()) {
                Ok(()) => Ok(()),
                Err(e) => {
                    error!("update_power_preferences failed: {:#}", e);
                    Err(MethodErr::failed("Failed to update power preferences"))
                }
            }
        });
        b.method(
            "SetLogLevel",
            ("level",),
            (),
            move |_, _, (level_raw,): (u8,)| {
                let level = match level_raw {
                    0 => LevelFilter::Off,
                    1 => LevelFilter::Error,
                    2 => LevelFilter::Warn,
                    3 => LevelFilter::Info,
                    4 => LevelFilter::Debug,
                    5 => LevelFilter::Trace,
                    _ => return Err(MethodErr::failed("Unsupported log level value")),
                };
                log::set_max_level(level);
                Ok(())
            },
        );
        let conn_clone = conn.clone();
        b.method_with_cr_async(
            "SetProcessState",
            ("ProcessId", "ProcessState"),
            (),
            move |mut sender_context, cr, (process_id, process_state): (u32, u8)| {
                let context: Option<&mut DbusContext> = cr.data_mut(sender_context.path());
                let sched_ctx = context.and_then(|ctx| ctx.scheduler_context.clone());
                let sender_bus_name = sender_context.message().sender().map(|s| s.to_string());
                let sender_euid = get_sender_euid(conn_clone.clone(), sender_bus_name);
                async move {
                    let Some(sched_ctx) = sched_ctx else {
                        return sender_context.reply(Err(MethodErr::failed("no schedqos context")));
                    };

                    let sender_euid = match sender_euid.await {
                        Ok(euid) => euid,
                        Err(e) => {
                            error!("failed to get sender euid: {:#}", e);
                            return sender_context
                                .reply(Err(MethodErr::failed("failed to get sender info")));
                        }
                    };

                    match set_process_state(sched_ctx, process_id, process_state, sender_euid) {
                        Ok(_) => sender_context.reply(Ok(())),
                        Err(e) => {
                            error!("change_process_state failed: {:#}, pid={}", e, process_id);
                            sender_context.reply(Err(e.to_dbus_error()))
                        }
                    }
                }
            },
        );
        let conn_clone = conn.clone();
        b.method_with_cr_async(
            "SetThreadState",
            ("ProcessId", "ThreadId", "ThreadState"),
            (),
            move |mut sender_context, cr, (process_id, thread_id, thread_state): (u32, u32, u8)| {
                let context: Option<&mut DbusContext> = cr.data_mut(sender_context.path());
                let sched_ctx = context.and_then(|ctx| ctx.scheduler_context.clone());
                let sender_bus_name = sender_context.message().sender().map(|s| s.to_string());
                let sender_euid = get_sender_euid(conn_clone.clone(), sender_bus_name);
                async move {
                    let Some(sched_ctx) = sched_ctx else {
                        return sender_context.reply(Err(MethodErr::failed("no schedqos context")));
                    };

                    let sender_euid = match sender_euid.await {
                        Ok(euid) => euid,
                        Err(e) => {
                            error!("failed to get sender euid: {:#}", e);
                            return sender_context
                                .reply(Err(MethodErr::failed("failed to get sender info")));
                        }
                    };

                    match set_thread_state(
                        sched_ctx,
                        process_id,
                        thread_id,
                        thread_state,
                        sender_euid,
                    ) {
                        Ok(_) => sender_context.reply(Ok(())),
                        Err(e) => {
                            error!("change_thread_state failed: {:#}, pid={}", e, process_id);
                            sender_context.reply(Err(e.to_dbus_error()))
                        }
                    }
                }
            },
        );
        b.method(
            "ReportBackgroundProcesses",
            ("raw_bytes",),
            (),
            move |_, _, (raw_bytes,): (Vec<u8>,)| {
                use system_api::resource_manager::report_background_processes::Component;

                let report_bk_processes: system_api::resource_manager::ReportBackgroundProcesses =
                    match protobuf::Message::parse_from_bytes(&raw_bytes) {
                        Ok(result) => result,
                        Err(e) => {
                            error!(
                                "Failed to parse ReportBackgroundProcesses protobuf: {:#}",
                                e
                            );
                            return Err(MethodErr::failed(
                                "Failed to parse ReportBackgroundProcesses protobuf",
                            ));
                        }
                    };
                let browser_type: memory::BrowserType =
                    match report_bk_processes.component.enum_value() {
                        Ok(Component::ASH) => memory::BrowserType::Ash,
                        Ok(Component::LACROS) => memory::BrowserType::Lacros,
                        Err(enum_raw) => {
                            error!(
                                "ReportBackgroundProcesses Component is unknown, enum_raw: {}",
                                enum_raw
                            );
                            return Err(MethodErr::failed(
                                "ReportBackgroundProcesses Component is unknown",
                            ));
                        }
                    };
                memory::set_background_processes(browser_type, report_bk_processes.pids);
                Ok(())
            },
        );
        b.method(
            "ReportBrowserProcesses",
            ("raw_bytes",),
            (),
            move |_, _, (raw_bytes,): (Vec<u8>,)| {
                use system_api::resource_manager::BrowserType;

                let report_browser_processes: system_api::resource_manager::ReportBrowserProcesses =
                    match protobuf::Message::parse_from_bytes(&raw_bytes) {
                        Ok(result) => result,
                        Err(e) => {
                            error!("Failed to parse ReportBrowserProcesses protobuf: {:#}", e);
                            return Err(MethodErr::failed(
                                "Failed to parse ReportBrowserProcesses protobuf",
                            ));
                        }
                    };
                let browser_type: memory::BrowserType =
                    match report_browser_processes.browser_type.enum_value() {
                        Ok(BrowserType::ASH) => memory::BrowserType::Ash,
                        Ok(BrowserType::LACROS) => memory::BrowserType::Lacros,
                        Err(enum_raw) => {
                            error!(
                                "ReportBrowserProcesses browser type is unknown, enum_raw: {}",
                                enum_raw
                            );
                            return Err(MethodErr::failed(
                                "ReportBrowserProcesses browser type is unknown",
                            ));
                        }
                    };
                let mut background_pids = Vec::<i32>::new();
                let mut protected_pids = Vec::<i32>::new();
                for process in report_browser_processes.processes {
                    // The focused tab processes are not used yet.
                    if process.focused {
                        continue;
                    }
                    if process.protected {
                        protected_pids.push(process.pid);
                    } else {
                        background_pids.push(process.pid);
                    }
                }
                memory::set_browser_processes(browser_type, background_pids, protected_pids);
                Ok(())
            },
        );

        // Advertise the signals.
        b.signal::<(u8, u64), _>(
            "MemoryPressureChrome",
            ("pressure_level", "reclaim_target_kb"),
        );
        b.signal::<(u8, u64), _>(
            "MemoryPressureArcvm",
            ("pressure_level", "reclaim_target_kb"),
        );
    })
}

fn set_vm_boot_mode(context: DbusContext, mode: common::VmBootMode) -> Result<()> {
    if !common::is_vm_boot_mode_enabled() {
        bail!("VM boot mode is not enabled");
    }
    common::set_vm_boot_mode(context.power_preferences_manager.as_ref(), mode).map_err(|e| {
        error!("set_vm_boot_mode failed: {:#}", e);
        MethodErr::failed("Failed to set VM boot mode")
    })?;

    context
        .reset_vm_boot_mode_timer_id
        .fetch_add(1, Ordering::Relaxed);

    if mode == common::VmBootMode::Active {
        let timer_id = context.reset_vm_boot_mode_timer_id.load(Ordering::Relaxed);
        let power_preferences_manager = context.power_preferences_manager.clone();
        tokio::spawn(async move {
            tokio::time::sleep(DEFAULT_VM_BOOT_TIMEOUT).await;
            if timer_id == context.reset_vm_boot_mode_timer_id.load(Ordering::Relaxed)
                && common::set_vm_boot_mode(
                    power_preferences_manager.as_ref(),
                    common::VmBootMode::Inactive,
                )
                .is_err()
            {
                error!("Reset VM boot mode failed.");
            }
        });
    }
    Ok(())
}

fn on_battery_saver_mode_change(context: DbusContext, raw_bytes: Vec<u8>) -> Result<()> {
    let bsm_state: BatterySaverModeState = protobuf::Message::parse_from_bytes(&raw_bytes)?;

    let mode = if bsm_state.enabled() {
        common::BatterySaverMode::Active
    } else {
        common::BatterySaverMode::Inactive
    };

    common::on_battery_saver_mode_change(context.power_preferences_manager.as_ref(), mode)
        .map_err(|e| {
            error!("on_battery_saver_mode_change failed: {:#}", e);
            MethodErr::failed("Failed to set battery saver mode")
        })?;

    Ok(())
}

async fn init_battery_saver_mode(context: DbusContext, conn: Arc<SyncConnection>) -> Result<()> {
    let powerd_proxy = Proxy::new(
        POWERD_INTERFACE_NAME,
        POWERD_PATH_NAME,
        Duration::from_millis(1000),
        conn,
    );

    let (powerd_response,): (Vec<u8>,) = powerd_proxy
        .method_call(POWERD_INTERFACE_NAME, "GetBatterySaverModeState", ())
        .await?;

    on_battery_saver_mode_change(context.clone(), powerd_response)
}

async fn memory_checker_wait(pressure_result: &Result<memory::PressureStatus>) {
    const MEMORY_USAGE_POLL_INTERVAL: u64 = 1000;

    match feature::is_feature_enabled(VARIABLE_TIME_MEMORY_SIGNAL_FEATURE_NAME) {
        Ok(true) => {
            // Stop waiting if there is 150 ms stall time in 1000 ms window.
            const STALL_MS: u64 = 150;
            const WINDOW_MS: u64 = 1000;

            // Wait longer when the current memory pressure is low.
            const MIN_WAITING_MS: u64 = 500;
            const MAX_WAITING_MS_NO_PRESSURE: u64 = 10000;
            const MAX_WAITING_MS_MODERATE_PRESSURE: u64 = 5000;
            const MAX_WAITING_MS_CRITICAL_PRESSURE: u64 = 1000;

            let max_waiting_ms = match pressure_result {
                Ok(pressure_status) => match pressure_status.chrome_level {
                    memory::PressureLevelChrome::None => MAX_WAITING_MS_NO_PRESSURE,
                    memory::PressureLevelChrome::Moderate => MAX_WAITING_MS_MODERATE_PRESSURE,
                    memory::PressureLevelChrome::Critical => MAX_WAITING_MS_CRITICAL_PRESSURE,
                },
                Err(e) => {
                    error!("get_memory_pressure_status() failed: {}", e);
                    MAX_WAITING_MS_NO_PRESSURE
                }
            };

            // Waiting for certain range of duration. Interrupt if PSI memory stall exceeds the
            // threshold.
            let wait_result = psi::wait_psi_monitor_memory_event(
                STALL_MS,
                WINDOW_MS,
                MIN_WAITING_MS,
                max_waiting_ms,
            )
            .await;
            if wait_result.is_err() {
                error!(
                    "wait_psi_monitor_memory_event returns error: {:?}",
                    wait_result
                );
                // Fallback to 1 second waiting.
                tokio::time::sleep(Duration::from_millis(MEMORY_USAGE_POLL_INTERVAL)).await;
            }
        }
        _ => {
            tokio::time::sleep(Duration::from_millis(MEMORY_USAGE_POLL_INTERVAL)).await;
        }
    }
}

fn report_notification_count(notification_count: i32) -> Result<()> {
    let metrics = metrics_rs::MetricsLibrary::get().context("MetricsLibrary::get() failed")?;

    // Shall panic on poisoned mutex.
    metrics
        .lock()
        .expect("Lock MetricsLibrary object failed")
        .send_to_uma(
            "Platform.Resourced.MemoryNotificationCountTenMinutes", // Metric name
            notification_count,                                     // Sample
            0,                                                      // Min
            1000,                                                   // Max
            50,                                                     // Number of buckets
        )?;
    Ok(())
}

pub async fn service_main() -> Result<()> {
    let root = Path::new("/");
    let scheduler_context = match qos::create_schedqos_context() {
        Ok(ctx) => Some(Arc::new(Mutex::new(ctx))),
        Err(e) => {
            error!("failed to initialize schedqos context: {e}");
            None
        }
    };
    let context = DbusContext {
        power_preferences_manager: Arc::new(power::new_directory_power_preferences_manager(root)),
        reset_game_mode_timer_id: Arc::new(AtomicUsize::new(0)),
        reset_fullscreen_video_timer_id: Arc::new(AtomicUsize::new(0)),
        reset_vm_boot_mode_timer_id: Arc::new(AtomicUsize::new(0)),
        scheduler_context,
    };

    let (io_resource, conn) = connection::new_system_sync()?;

    // io_resource must be awaited to start receiving D-Bus message.
    let _handle = tokio::spawn(async {
        let err = io_resource.await;
        panic!("Lost connection to D-Bus: {}", err);
    });

    feature::start_feature_monitoring(conn.as_ref())
        .await
        .context("start feature monitoring")?;

    let vmms_client = VmMemoryManagementClient::new(conn.clone())
        .await
        .context("create VmMemoryManagementClient")?;

    conn.request_name(SERVICE_NAME, false, true, false).await?;

    let mut cr = Crossroads::new();

    // Enable asynchronous methods. Incoming method calls are spawned as separate tasks if
    // necessary.
    cr.set_async_support(Some((
        conn.clone(),
        Box::new(|x| {
            tokio::spawn(x);
        }),
    )));

    let token = register_interface(&mut cr, conn.clone());
    cr.insert(PATH_NAME, &[token], context.clone());

    #[cfg(feature = "vm_grpc")]
    {
        // This block does a 1 time check at resourced startup to see if borealis vm
        // is already running.  The `await` has a blocking timeout of 2 seconds
        // (2 embedded dbus calls).  If there is no vm running (expected case), no grpc server
        // will be started.  If resourced was respawned while a borealis session was
        // active (corner case), this logic will start a vm_grpc server and re-establish the
        // grpc link.
        // Internal failures will be logged and ignored.  We don't interfere with resourced
        // operations if grpc server/client fails.
        let _ =
            crate::vm_grpc::vm_grpc_util::handle_startup_borealis_state_async(conn.clone()).await;
        let _ = crate::vm_grpc::vm_grpc_util::register_dbus_hooks_async(conn.clone()).await;
    }

    if common::is_vm_boot_mode_enabled() {
        // Receive VmStartingUpSignal for VM boot mode
        let vm_starting_up_rule =
            MatchRule::new_signal(VMCONCIEGE_INTERFACE_NAME, "VmStartingUpSignal");
        conn.add_match_no_cb(&vm_starting_up_rule.match_str())
            .await?;
        let context2 = context.clone();
        conn.start_receive(
            vm_starting_up_rule,
            Box::new(move |_, _| {
                match set_vm_boot_mode(context2.clone(), common::VmBootMode::Active) {
                    Ok(_) => true,
                    Err(e) => {
                        error!("Failed to initalize VM boot boosting. {}", e);
                        false
                    }
                }
            }),
        );
        let vm_complete_boot_rule =
            MatchRule::new_signal(VMCONCIEGE_INTERFACE_NAME, "VmGuestUserlandReadySignal");
        conn.add_match_no_cb(&vm_complete_boot_rule.match_str())
            .await?;
        let cb_context = context.clone();
        conn.start_receive(
            vm_complete_boot_rule,
            Box::new(move |_, _| {
                match set_vm_boot_mode(cb_context.clone(), common::VmBootMode::Inactive) {
                    Ok(_) => true,
                    Err(e) => {
                        error!("Failed to stop VM boot boosting. {}", e);
                        false
                    }
                }
            }),
        );
    }

    if init_battery_saver_mode(context.clone(), conn.clone())
        .await
        .is_err()
    {
        error!("init_battery_saver_mode failed");
    }

    // Registers callbacks for `BatterySaverModeStateChanged` from powerd.
    const BATTERY_SAVER_MODE_EVENT: &str = "BatterySaverModeStateChanged";
    let battery_saver_mode_rule =
        MatchRule::new_signal(POWERD_INTERFACE_NAME, BATTERY_SAVER_MODE_EVENT);
    conn.add_match_no_cb(&battery_saver_mode_rule.match_str())
        .await?;

    conn.start_receive(
        battery_saver_mode_rule,
        Box::new(move |msg, _| match msg.read1() {
            Ok(bytes) => match on_battery_saver_mode_change(context.clone(), bytes) {
                Ok(()) => true,
                Err(e) => {
                    error!("error handling Battery Saver Mode change. {}", e);
                    false
                }
            },
            Err(e) => {
                error!(
                    "error reading D-Bus message {}. {}",
                    BATTERY_SAVER_MODE_EVENT, e
                );
                false
            }
        }),
    );

    conn.start_receive(
        MatchRule::new_method_call(),
        Box::new(move |msg, conn| match cr.handle_message(msg, conn) {
            Ok(()) => true,
            Err(()) => {
                error!("error handling D-Bus message");
                false
            }
        }),
    );

    if let Err(err) = feature::initialize_feature(VARIABLE_TIME_MEMORY_SIGNAL_FEATURE_NAME, false) {
        error!(
            "Failed to update feature {}: {}",
            VARIABLE_TIME_MEMORY_SIGNAL_FEATURE_NAME, err
        );
    }

    // Reports memory pressure notification count every 10 minutes.
    let notification_count = Arc::new(AtomicI32::new(0));
    let notification_count_clone = notification_count.clone();
    tokio::spawn(async move {
        loop {
            // 10 minutes interval.
            tokio::time::sleep(Duration::from_secs(10 * 60)).await;

            let count = notification_count_clone.load(Ordering::Relaxed);

            if let Err(err) = report_notification_count(count) {
                error!("Failed to report notification count: {}", err);
            }
            notification_count_clone.store(0, Ordering::Relaxed);
        }
    });

    // The memory checker loop.
    loop {
        let pressure_result = memory::get_memory_pressure_status(&vmms_client).await;

        // Send memory pressure notification.
        if let Ok(pressure_status) = pressure_result {
            send_pressure_signal(
                &conn,
                "MemoryPressureChrome",
                pressure_status.chrome_level as u8,
                pressure_status.chrome_reclaim_target_kb,
            );
            if pressure_status.arcvm_level != memory::PressureLevelArcvm::None {
                send_pressure_signal(
                    &conn,
                    "MemoryPressureArcvm",
                    pressure_status.arcvm_level as u8,
                    pressure_status.arcvm_reclaim_target_kb,
                );
            }
            if pressure_status.arc_container_level != memory::PressureLevelArcContainer::None {
                send_pressure_signal(
                    &conn,
                    "MemoryPressureArcContainer",
                    pressure_status.arc_container_level as u8,
                    pressure_status.arc_container_reclaim_target_kb,
                );
            }
        }

        notification_count.fetch_add(1, Ordering::Relaxed);

        memory_checker_wait(&pressure_result).await;
    }
}
