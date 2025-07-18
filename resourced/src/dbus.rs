// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::convert::TryFrom;
use std::fs;
use std::path::Path;
use std::path::PathBuf;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;
use std::time::Instant;

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
use log::info;
use log::warn;
use log::LevelFilter;
use system_api::battery_saver::BatterySaverModeState;
use tokio::sync::Notify;

use crate::arch;
use crate::common;
use crate::config::ConfigProvider;
use crate::config::ThermalConfig;
use crate::feature;
use crate::feature::is_feature_enabled;
use crate::feature::register_feature;
use crate::memory;
use crate::memory::MemInfo;
use crate::memory::MemoryMarginsBps;
use crate::memory::PsiMemoryHandler;
use crate::memory::PsiPolicyResult;
use crate::memory::MAX_PSI_ERROR_TYPE;
use crate::memory::UMA_NAME_PSI_POLICY_ERROR;
use crate::metrics;
use crate::power;
use crate::proc::load_euid;
use crate::process_stats;
use crate::psi::PsiWatcher;
use crate::psi::Target;
use crate::qos;
use crate::qos::send_set_process_state_failure_to_uma;
use crate::qos::send_set_thread_state_failure_to_uma;
use crate::qos::set_process_state;
use crate::qos::set_thread_state;
use crate::qos::SchedQosContext;
use crate::qos::QOS_ERROR_NO_CONTEXT;
use crate::qos::QOS_ERROR_NO_SENDER;
use crate::realtime;
use crate::swappiness_config::new_swappiness_config;
use crate::swappiness_config::SwappinessConfig;
use crate::sync::NoPoison;
use crate::vm_memory_management_client::VmMemoryManagementClient;

const SERVICE_NAME: &str = "org.chromium.ResourceManager";
const PATH_NAME: &str = "/org/chromium/ResourceManager";
const INTERFACE_NAME: &str = SERVICE_NAME;

const VMCONCIEGE_INTERFACE_NAME: &str = "org.chromium.VmConcierge";
const POWERD_INTERFACE_NAME: &str = "org.chromium.PowerManager";
const POWERD_PATH_NAME: &str = "/org/chromium/PowerManager";

pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(5);
const THERMAL_POLLING_PERIOD: Duration = Duration::from_secs(5);

// The timeout in second for VM boot mode. Currently this is
// 60seconds which is long enough for booting a VM on low-end DUTs.
const DEFAULT_VM_BOOT_TIMEOUT: Duration = Duration::from_secs(60);

type PowerPreferencesManager =
    power::DirectoryPowerPreferencesManager<power::DirectoryPowerSourceProvider>;

// Context data for the D-Bus service.
#[derive(Clone)]
struct DbusContext {
    power_preferences_manager: Arc<PowerPreferencesManager>,

    // Timer ids for skipping out-of-dated timer events.
    reset_game_mode_timer_id: Arc<AtomicUsize>,
    reset_fullscreen_video_timer_id: Arc<AtomicUsize>,
    reset_vm_boot_mode_timer_id: Arc<AtomicUsize>,

    thermal_config_mutex: Option<Arc<Mutex<ThermalConfig>>>,

    scheduler_context: Option<Arc<Mutex<SchedQosContext>>>,
}

fn send_pressure_signal(
    conn: &SyncConnection,
    signal_name: &str,
    level: u8,
    reclaim_target_kb: u64,
    discard_type_option: Option<u8>,
) {
    let signal_origin_timestamp_ms = match get_monotonic_timestamp_ms() {
        Ok(timestamp) => timestamp,
        Err(_) => {
            error!("Failed to get current time.");
            -1
        }
    };

    let msg = Message::signal(
        &PATH_NAME.into(),
        &INTERFACE_NAME.into(),
        &signal_name.into(),
    )
    // Since the origin timestamp originates from CLOCK_MONOTONIC,
    // it should not be used from any guest context. It can only be compared within the host.
    .append3(level, reclaim_target_kb, signal_origin_timestamp_ms);

    let msg = if let Some(discard_type) = discard_type_option {
        // Appends argument discard type for Chrome.
        msg.append1(discard_type)
    } else {
        msg
    };

    if conn.send(msg).is_err() {
        error!("Send Chrome pressure signal failed.");
    }
}

// Call swap_management SwapSetSwappiness when set_game_mode returns TuneSwappiness.
fn set_game_mode_and_tune_swappiness(
    power_preferences_manager: &dyn power::PowerPreferencesManager,
    mode: common::GameMode,
    swappiness_config: SwappinessConfig,
) -> Result<()> {
    let tuning = common::set_game_mode(power_preferences_manager, mode, PathBuf::from("/"));
    swappiness_config.update_tuning(tuning);
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

fn override_critical_if_necessary(original_critical_margin: u32) -> u32 {
    // TODO(b:365580055, b:364375966) Remove this override when a long-term solution is
    // found for the xol and lotso/jubilant performance issues.
    let critical_offset = match fs::read_to_string(Path::new("/run/chromeos-config/v1/name")) {
        Ok(device_name) => match device_name.as_str() {
            "xol" | "lotso" | "jubilant" | "marasov" | "ciri" | "rull" => 700,
            _ => 0,
        },
        _ => 0,
    };

    if critical_offset == 0 {
        return original_critical_margin;
    }

    // Never override a critical margin above 1500.
    let overridden_critical_margin =
        std::cmp::min(1500, original_critical_margin + critical_offset);

    if overridden_critical_margin != original_critical_margin {
        warn!(
            "Overriding critical margin by: Original: {} Overridden to: {}",
            original_critical_margin, overridden_critical_margin
        );
    }

    overridden_critical_margin
}

fn register_interface(
    cr: &mut Crossroads,
    conn: Arc<SyncConnection>,
    swappiness_config: SwappinessConfig,
) -> IfaceToken<DbusContext> {
    cr.register(INTERFACE_NAME, |b: &mut IfaceBuilder<DbusContext>| {
        b.method(
            "GetAvailableMemoryKB",
            (),
            ("available",),
            move |_, _, ()| {
                let game_mode = common::get_game_mode();
                match MemInfo::load() {
                    Ok(meminfo) => Ok((memory::get_background_available_memory_kb(
                        &meminfo, game_mode, false,
                    ),)),
                    Err(_) => Err(MethodErr::failed("Couldn't get available memory")),
                }
            },
        );
        b.method(
            "GetForegroundAvailableMemoryKB",
            (),
            ("available",),
            move |_, _, ()| match MemInfo::load() {
                Ok(meminfo) => Ok((memory::get_foreground_available_memory_kb(&meminfo),)),
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
                Ok((margins.critical, margins.moderate))
            },
        );
        b.method(
            "GetComponentMemoryMarginsKB",
            (),
            ("margins",),
            move |_, _, ()| {
                let margins = memory::get_component_margins_kb();
                let result = HashMap::from([
                    ("ChromeModerate", margins.chrome_moderate),
                    ("ChromeCritical", margins.chrome_critical),
                    ("ChromeCriticalProtected", margins.chrome_critical_protected),
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
            "SetMemoryMargins",
            ("raw_bytes",),
            (),
            move |_, _, (raw_bytes,): (Vec<u8>,)| {
                let memory_margins: system_api::resource_manager::MemoryMargins =
                    match protobuf::Message::parse_from_bytes(&raw_bytes) {
                        Ok(result) => result,
                        Err(e) => {
                            error!("Failed to parse MemoryMargins protobuf: {:#}", e);
                            return Err(MethodErr::failed(
                                "Failed to parse MemoryMargins protobuf",
                            ));
                        }
                    };
                let margins_bps = MemoryMarginsBps {
                    critical: (override_critical_if_necessary(memory_margins.critical_bps)).into(),
                    moderate: memory_margins.moderate_bps.into(),
                    critical_protected: memory_margins.critical_protected_bps.into(),
                };
                memory::set_memory_margins_bps(margins_bps);
                Ok(())
            },
        );
        // TODO(vovoy): remove this method.
        b.method(
            "SetMemoryMarginsBps",
            ("critical_bps", "moderate_bps"),
            ("critical", "moderate"),
            move |_, _, (critical_bps, moderate_bps): (u32, u32)| {
                // Set critical protected margin the same as critical margin for the old API.
                let margins_bps = MemoryMarginsBps {
                    critical: (override_critical_if_necessary(critical_bps)).into(),
                    moderate: moderate_bps.into(),
                    critical_protected: critical_bps.into(),
                };
                memory::set_memory_margins_bps(margins_bps);
                let margins = memory::get_memory_margins_kb();
                Ok((margins.critical, margins.moderate))
            },
        );
        b.method("GetGameMode", (), ("game_mode",), move |_, _, ()| {
            Ok((common::get_game_mode() as u8,))
        });
        let swappiness_config_clone = swappiness_config.clone();
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
                    swappiness_config_clone.clone(),
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
        let swappiness_config_clone = swappiness_config.clone();
        b.method(
            "SetGameModeWithTimeout",
            ("game_mode", "timeout_sec"),
            ("origin_game_mode",),
            move |_, context, (mode_raw, timeout_raw): (u8, u32)| {
                let old_game_mode = common::get_game_mode();
                let mode = common::GameMode::try_from(mode_raw)
                    .map_err(|_| MethodErr::failed("Unsupported game mode value"))?;
                let timeout = Duration::from_secs(timeout_raw.into());
                set_game_mode_and_tune_swappiness(
                    context.power_preferences_manager.as_ref(),
                    mode,
                    swappiness_config_clone.clone(),
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
                let swappiness_config_clone2 = swappiness_config_clone.clone();
                tokio::spawn(async move {
                    tokio::time::sleep(timeout).await;
                    // If the timer id is changed, this event is canceled.
                    if timer_id == reset_game_mode_timer_id.load(Ordering::Relaxed)
                        && set_game_mode_and_tune_swappiness(
                            power_preferences_manager.as_ref(),
                            common::GameMode::Off,
                            swappiness_config_clone2.clone(),
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
            Ok((common::get_rtc_audio_active() as u8,))
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
                        error!("set_rtc_audio_active failed: {:#}", e);
                        Err(MethodErr::failed("Failed to set RTC audio activity"))
                    }
                }
            },
        );
        b.method("GetFullscreenVideo", (), ("mode",), move |_, _, ()| {
            Ok((common::get_fullscreen_video() as u8,))
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
            "GetThermalState",
            (),
            ("thermal_state",),
            move |_, _, ()| Ok((common::get_thermal_state() as u8,)),
        );
        b.method(
            "SetThermalTripTemp",
            ("temp",),
            (),
            move |_, context, (temp,): (i32,)| {
                if let Some(thermal_config_mutex) = context.thermal_config_mutex.as_ref() {
                    let mut thermal_config = thermal_config_mutex.do_lock();
                    thermal_config.trip_temp = temp;
                }
                Ok(())
            },
        );
        b.method(
            "GetThermalTripTemp",
            (),
            ("temp",),
            move |_, context, ()| match context.thermal_config_mutex.as_ref() {
                Some(thermal_config_mutex) => {
                    let thermal_config = thermal_config_mutex.do_lock();
                    Ok((thermal_config.trip_temp,))
                }
                None => Ok((0_i32,)),
            },
        );
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
                        send_set_process_state_failure_to_uma(QOS_ERROR_NO_CONTEXT);
                        return sender_context.reply(Err(MethodErr::failed("no schedqos context")));
                    };

                    let sender_euid = match sender_euid.await {
                        Ok(euid) => euid,
                        Err(e) => {
                            error!("failed to get sender euid: {:#}", e);
                            send_set_process_state_failure_to_uma(QOS_ERROR_NO_SENDER);
                            return sender_context
                                .reply(Err(MethodErr::failed("failed to get sender info")));
                        }
                    };

                    match set_process_state(sched_ctx, process_id, process_state, sender_euid) {
                        Ok(_) => sender_context.reply(Ok(())),
                        Err(e) => {
                            error!("change_process_state failed: {:#}, pid={}", e, process_id);
                            send_set_process_state_failure_to_uma(e.to_uma_enum_sample());
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
                        send_set_thread_state_failure_to_uma(QOS_ERROR_NO_CONTEXT);
                        return sender_context.reply(Err(MethodErr::failed("no schedqos context")));
                    };

                    let sender_euid = match sender_euid.await {
                        Ok(euid) => euid,
                        Err(e) => {
                            error!("failed to get sender euid: {:#}", e);
                            send_set_thread_state_failure_to_uma(QOS_ERROR_NO_SENDER);
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
                            send_set_thread_state_failure_to_uma(e.to_uma_enum_sample());
                            sender_context.reply(Err(e.to_dbus_error()))
                        }
                    }
                }
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
                let mut background_tab_processes = Vec::<memory::TabProcess>::new();
                let mut protected_tab_processes = Vec::<memory::TabProcess>::new();

                // The tab processes' last visible times are sent by Chrome as millisecond values
                // from CLOCK_MONOTONIC.
                // Rust doesn't support initializing 'Instant's from external values, so the
                // raw CLOCK_MONOTONIC value is needed as a reference.
                let current_timestamp_ms = match get_monotonic_timestamp_ms() {
                    Ok(timestamp) => timestamp,
                    Err(_) => {
                        error!("Failed to get current time.");
                        return Err(MethodErr::failed("Failed to get current time."));
                    }
                };
                let now = Instant::now();

                for tab_process in report_browser_processes.processes {
                    // The focused tab processes are not used yet.
                    if tab_process.focused {
                        continue;
                    }

                    // To obtain the 'Instant' at which the tab was last visible,
                    // subtract the number of milliseconds since the tab was visible from the
                    // current Instant.
                    let last_visible_instant = now
                        - Duration::from_millis(
                            (current_timestamp_ms - tab_process.last_visible_ms) as u64,
                        );

                    if tab_process.protected {
                        &mut protected_tab_processes
                    } else {
                        &mut background_tab_processes
                    }
                    .push(memory::TabProcess {
                        pid: tab_process.pid,
                        last_visible: last_visible_instant,
                    });
                }
                memory::set_browser_tab_processes(
                    browser_type,
                    background_tab_processes,
                    protected_tab_processes,
                );
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

async fn memory_checker_wait(
    pressure_result: &Result<memory::PressureStatus>,
    psi_watcher: Option<&mut PsiWatcher>,
) {
    const MEMORY_USAGE_POLL_INTERVAL_DEFAULT: Duration = Duration::from_millis(1000);
    let Some(psi_watcher) = psi_watcher else {
        // If opening psi fd had failed, fallback to 1 second interval.
        tokio::time::sleep(MEMORY_USAGE_POLL_INTERVAL_DEFAULT).await;
        return;
    };

    // Wait longer when the current memory pressure is low.
    const MAX_WAITING_NO_PRESSURE: Duration = Duration::from_millis(10000);
    const MAX_WAITING_MODERATE_PRESSURE: Duration = Duration::from_millis(5000);
    const MAX_WAITING_CRITICAL_PRESSURE: Duration = Duration::from_millis(1000);

    let max_waiting = match pressure_result {
        Ok(pressure_status) => match pressure_status.chrome_level {
            memory::PressureLevelChrome::None => MAX_WAITING_NO_PRESSURE,
            memory::PressureLevelChrome::Moderate => MAX_WAITING_MODERATE_PRESSURE,
            memory::PressureLevelChrome::DiscardUnprotected
            | memory::PressureLevelChrome::Critical => MAX_WAITING_CRITICAL_PRESSURE,
        },
        Err(e) => {
            error!("get_memory_pressure_status() failed: {}", e);
            MAX_WAITING_NO_PRESSURE
        }
    };

    // Waiting for certain range of duration. Interrupt if PSI memory stall exceeds the
    // threshold.
    if let Ok(Err(e)) = tokio::time::timeout(max_waiting, psi_watcher.wait()).await {
        error!("wait_psi_monitor_memory_event returns error: {:?}", e);
        tokio::time::sleep(MEMORY_USAGE_POLL_INTERVAL_DEFAULT).await;
    }
}

fn report_notification_count(notification_count: i32) -> Result<()> {
    metrics::send_to_uma(
        "Platform.Resourced.MemoryNotificationCountTenMinutes", // Metric name
        notification_count,                                     // Sample
        0,                                                      // Min
        1000,                                                   // Max
        50,                                                     // Number of buckets
    )
}

fn report_memory_stats(stats: process_stats::MemStats) -> Result<()> {
    for (process_kind, stats) in stats.iter().enumerate() {
        for (mem_kind, usage_bytes) in stats.iter().enumerate() {
            // Max process allocation size in megabytes, used as an upper bound
            // for UMA histograms (these are all consumer devices, and 64 GB
            // should be good for a few more years).
            const MAX_MEM_SIZE_MIB: i32 = 64 * 1024;
            metrics::send_to_uma(
                &process_stats::get_metric_name(process_kind, mem_kind),
                (usage_bytes / 1024 / 1024) as i32,
                1,
                MAX_MEM_SIZE_MIB,
                50,
            )?;
        }
    }
    Ok(())
}

fn get_monotonic_timestamp_ms() -> Result<i64> {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };

    // SAFETY: Safe because the only side-effects of this function are modifications via the
    // passed pointer, and we pass a pointer of the proper type.
    let current_timestamp_ms =
        if unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts as *mut libc::timespec) }
            == 0
        {
            // On 32bit platforms, tv_sec and tv_nsec are i32. Upcast to u64 here for compatibility.
            (ts.tv_sec as u64) * 1000 + (ts.tv_nsec as u64) / 1_000_000
        } else {
            bail!("Failed to get current time.");
        };

    Ok(current_timestamp_ms as i64)
}

async fn psi_memory_handler_loop(
    root: &Path,
    conn: &Arc<SyncConnection>,
    vmms_client: &VmMemoryManagementClient,
    feature_notify: &Arc<Notify>,
    notification_count: &Arc<AtomicI32>,
) -> PsiPolicyResult<()> {
    let mut handler = PsiMemoryHandler::new(root)?;
    loop {
        if let Some(pressure_status) = handler
            .monitor_memory_pressure(vmms_client, feature_notify)
            .await?
        {
            send_pressure_signals(conn, &pressure_status);
            notification_count.fetch_add(1, Ordering::Relaxed);
        } else {
            // reclaim_on_memory_pressure() returns `None` if the feature flag or params is changed.
            if !is_feature_enabled(memory::PSI_MEMORY_POLICY_FEATURE_NAME).unwrap_or(false) {
                return Ok(());
            }
            handler.reload_feature_params();
        }
    }
}

async fn margin_memory_handler_loop(
    conn: &Arc<SyncConnection>,
    vmms_client: &VmMemoryManagementClient,
    notification_count: &Arc<AtomicI32>,
) {
    // Stop waiting if there is 150 ms stall time in 1000 ms window.
    const STALL_DURATION: Duration = Duration::from_millis(150);
    const WINDOW_DURATION: Duration = Duration::from_millis(1000);
    let mut psi_watcher =
        match PsiWatcher::new_memory_pressure(Target::Some, STALL_DURATION, WINDOW_DURATION) {
            Ok(psi_watcher) => Some(psi_watcher),
            Err(e) => {
                error!("failed to create psi watcher: {:?}", e);
                None
            }
        };

    loop {
        let pressure_result = memory::get_memory_pressure_status(vmms_client).await;

        // Send memory pressure notification.
        if let Ok(pressure_status) = pressure_result {
            send_pressure_signals(conn, &pressure_status);
        }

        notification_count.fetch_add(1, Ordering::Relaxed);

        memory_checker_wait(&pressure_result, psi_watcher.as_mut()).await;

        // The feature flag change is reflected in 10 seconds since memory_checker_wait() wakes
        // within 10 seconds at least.
        if is_feature_enabled(memory::PSI_MEMORY_POLICY_FEATURE_NAME).unwrap_or(false) {
            return;
        }
    }
}

fn send_pressure_signals(conn: &Arc<SyncConnection>, pressure_status: &memory::PressureStatus) {
    let (chrome_level, discard_type) = pressure_status.chrome_level.to_dbus_params();
    send_pressure_signal(
        conn,
        "MemoryPressureChrome",
        chrome_level,
        pressure_status.chrome_reclaim_target_kb,
        Some(discard_type),
    );
    if pressure_status.arc_container_level != memory::PressureLevelArcContainer::None {
        send_pressure_signal(
            conn,
            "MemoryPressureArcContainer",
            pressure_status.arc_container_level as u8,
            pressure_status.arc_container_reclaim_target_kb,
            None,
        );
    }
}

pub async fn service_main() -> Result<()> {
    let root = Path::new("/");
    let config_provider = ConfigProvider::from_root(root);
    let scheduler_context = match qos::create_schedqos_context(root) {
        Ok(ctx) => {
            let scheduler_context = Arc::new(Mutex::new(ctx));
            qos::register_features(root, scheduler_context.clone());
            Some(scheduler_context)
        }
        Err(e) => {
            error!("failed to initialize schedqos context: {e}");
            None
        }
    };
    let thermal_config_mutex: Option<Arc<Mutex<ThermalConfig>>> =
        match config_provider.read_thermal_config() {
            Ok(Some(config)) => Some(Arc::new(Mutex::new(config))),
            Ok(None) => None,
            Err(e) => {
                error!("failed to read thermal config: {e}");
                None
            }
        };
    let context = DbusContext {
        power_preferences_manager: Arc::new(power::new_directory_power_preferences_manager(
            root,
            config_provider,
        )),
        reset_game_mode_timer_id: Arc::new(AtomicUsize::new(0)),
        reset_fullscreen_video_timer_id: Arc::new(AtomicUsize::new(0)),
        reset_vm_boot_mode_timer_id: Arc::new(AtomicUsize::new(0)),
        thermal_config_mutex: thermal_config_mutex.clone(),
        scheduler_context,
    };

    let (io_resource, conn) = connection::new_system_sync()?;
    conn.set_signal_match_mode(true);

    // io_resource must be awaited to start receiving D-Bus message.
    let _handle = tokio::spawn(async {
        let err = io_resource.await;
        panic!("Lost connection to D-Bus: {}", err);
    });

    arch::init();

    realtime::register_features();

    let (swappiness_config, mut swappiness_proxy) = new_swappiness_config();
    memory::register_features(swappiness_config.clone());

    let conn_clone = conn.clone();
    tokio::spawn(async move {
        if let Err(err) = swappiness_proxy.run_proxy(conn_clone).await {
            error!("Error with swappiness proxy {:?}", err);
        }
    });

    if let Some(thermal_config_mutex) = thermal_config_mutex {
        if let Some(read_thermal_state) =
            common::thermal_state_callback(root.to_path_buf(), thermal_config_mutex)
        {
            let power_preferences_manager = context.power_preferences_manager.clone();
            tokio::spawn(async move {
                loop {
                    if common::update_thermal_state(&read_thermal_state) {
                        if let Err(e) =
                            common::update_power_preferences(power_preferences_manager.as_ref())
                        {
                            error!("update_power_preferences failed: {:#}", e);
                        }
                    }
                    tokio::time::sleep(THERMAL_POLLING_PERIOD).await;
                }
            });
        }
    }

    let psi_memory_policy_notify = Arc::new(Notify::new());
    let notify_cloned = psi_memory_policy_notify.clone();
    register_feature(
        memory::PSI_MEMORY_POLICY_FEATURE_NAME,
        false,
        Some(Box::new(move |_| {
            notify_cloned.notify_waiters();
        })),
    );

    feature::init(conn.as_ref())
        .await
        .context("start feature monitoring")?;

    let vmms_client = VmMemoryManagementClient::new(conn.clone())
        .await
        .context("create VmMemoryManagementClient")?;

    let mut cr = Crossroads::new();

    // Enable asynchronous methods. Incoming method calls are spawned as separate tasks if
    // necessary.
    cr.set_async_support(Some((
        conn.clone(),
        Box::new(|x| {
            tokio::spawn(x);
        }),
    )));

    let token = register_interface(&mut cr, conn.clone(), swappiness_config);
    cr.insert(PATH_NAME, &[token], context.clone());

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
                if let Err(e) = set_vm_boot_mode(context2.clone(), common::VmBootMode::Active) {
                    error!("Failed to initalize VM boot boosting. {}", e);
                }
                true
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
                if let Err(e) = set_vm_boot_mode(cb_context.clone(), common::VmBootMode::Inactive) {
                    error!("Failed to stop VM boot boosting. {}", e);
                }
                true
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
            Ok(bytes) => {
                if let Err(e) = on_battery_saver_mode_change(context.clone(), bytes) {
                    error!("error handling Battery Saver Mode change. {}", e);
                }
                true
            }
            Err(e) => {
                error!(
                    "error reading D-Bus message {}. {}",
                    BATTERY_SAVER_MODE_EVENT, e
                );
                true
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

    // Now that the callbacks are all initialized, we can advertise our service.
    conn.request_name(SERVICE_NAME, false, true, false).await?;

    // Reports memory pressure notification count and memory stats every 10 minutes.
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

            // Gathering memory stats is a non-trival amount of work, so do it on
            // a separate thread to avoid blocking resourced's other work.
            let res = tokio::task::spawn_blocking(|| {
                match process_stats::get_all_memory_stats("/proc", "/run") {
                    Ok(stats) => {
                        if let Err(err) = report_memory_stats(stats) {
                            error!("Failed to report memory stats: {}", err);
                        }
                    }
                    Err(e) => error!("Failed to gather memory stats {:?}", e),
                }
            })
            .await;
            if let Err(e) = res {
                error!("Error gathering memory stats {:?}", e);
            };
        }
    });

    loop {
        let use_psi_policy = match is_feature_enabled(memory::PSI_MEMORY_POLICY_FEATURE_NAME) {
            Ok(v) => v,
            Err(e) => {
                error!(
                    "Failed to get feature {}: {}",
                    memory::PSI_MEMORY_POLICY_FEATURE_NAME,
                    e
                );
                false
            }
        };
        if use_psi_policy {
            info!("Using psi memory policy");
            if let Err(e) = psi_memory_handler_loop(
                root,
                &conn,
                &vmms_client,
                &psi_memory_policy_notify,
                &notification_count,
            )
            .await
            {
                error!("Failed to run psi memory checker loop: {}", e);
                if let Err(e) = metrics::send_enum_to_uma(
                    UMA_NAME_PSI_POLICY_ERROR,
                    e.as_i32(),
                    MAX_PSI_ERROR_TYPE + 1,
                ) {
                    error!("Failed to send psi memory checker error to UMA: {}", e);
                }

                // wait 1 second to retry to avoid logging the error in a busy loop.
                tokio::time::sleep(Duration::from_secs(1)).await;
            }
        } else {
            info!("Using margin memory policy");
            margin_memory_handler_loop(&conn, &vmms_client, &notification_count).await;
        }
    }
}
