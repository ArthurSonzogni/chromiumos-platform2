// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements hibernate suspend functionality.

use std::fs::File;
use std::io::BufRead;
use std::io::Write;
use std::mem;
use std::process::Command;
use std::sync::RwLockReadGuard;
use std::thread;
use std::time::Duration;
use std::time::Instant;
use std::time::UNIX_EPOCH;

use anyhow::anyhow;
use anyhow::Context;
use anyhow::Result;
use libc::reboot;
use libc::RB_AUTOBOOT;
use libc::RB_POWER_OFF;
use log::debug;
use log::error;
use log::info;
use log::warn;
use nix::errno::Errno;
use regex::Regex;

use crate::cookie::set_hibernate_cookie;
use crate::cookie::HibernateCookieValue;
use crate::cryptohome;
use crate::hiberlog::redirect_log;
use crate::hiberlog::replay_logs;
use crate::hiberlog::reset_log;
use crate::hiberlog::HiberlogOut;
use crate::hiberutil::checked_command_output;
use crate::hiberutil::get_available_memory_mb;
use crate::hiberutil::get_kernel_restore_time;
use crate::hiberutil::get_page_size;
use crate::hiberutil::get_ram_size;
use crate::hiberutil::has_user_logged_out;
use crate::hiberutil::lock_process_memory;
use crate::hiberutil::path_to_stateful_block;
use crate::hiberutil::zram_get_bd_stats;
use crate::hiberutil::zram_is_writeback_enabled;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateOptions;
use crate::hiberutil::HibernateStage;
use crate::journal::LogFile;
use crate::journal::TimestampFile;
use crate::metrics::read_and_send_metrics;
use crate::metrics::DurationMetricUnit;
use crate::metrics::HibernateEvent;
use crate::metrics::METRICS_LOGGER;
use crate::snapdev::FrozenUserspaceTicket;
use crate::snapdev::SnapshotDevice;
use crate::snapdev::SnapshotMode;
use crate::swap_management::initiate_swap_zram_writeback;
use crate::swap_management::reclaim_all_processes;
use crate::swap_management::swap_zram_mark_idle;
use crate::swap_management::swap_zram_set_writeback_limit;
use crate::swap_management::WritebackMode;
use crate::update_engine::is_update_in_progress;
use crate::volume::ActiveMount;
use crate::volume::VolumeManager;
use crate::volume::VOLUME_MANAGER;

const DROP_CACHES_ATTR_PATH: &str = "/proc/sys/vm/drop_caches";

/// Value to tell zram to write back all eligible memory.
const ZRAM_WRITEBACK_LIMIT_MB_MAX: u32 = 32768;

/// Reason why an attempt to suspend was aborted
/// Values need to match CrosHibernateAbortReason in Chromium's enums.xml
enum SuspendAbortReason {
    // These values are persisted to logs. Entries should not be renumbered and
    // numeric values should never be reused.
    Other = 0,
    InsufficientFreeMemory = 1,
    InsufficientDiskSpace = 2,
    UpdateEngineActive = 3,
    NoHiberimage = 4,
    PriorUserLogout = 5,
    PinWeaverCredentialsExist = 6,
    Count = 7,
}

#[derive(PartialEq)]
enum ResumeTimeMetric {
    RestoreSystem,
    Total,
}

/// The SuspendConductor weaves a delicate baton to guide us through the
/// symphony of hibernation.
pub struct SuspendConductor<'a> {
    options: HibernateOptions,
    volume_manager: RwLockReadGuard<'a, VolumeManager>,
    timestamp_resumed: Option<Duration>,
}

impl SuspendConductor<'_> {
    /// Create a new SuspendConductor in preparation for imminent hibernation.
    pub fn new() -> Result<Self> {
        Ok(SuspendConductor {
            options: Default::default(),
            volume_manager: VOLUME_MANAGER.read().unwrap(),
            timestamp_resumed: None,
        })
    }

    /// Public entry point that hibernates the system, and returns either upon
    /// failure to hibernate or after the system has resumed from a successful
    /// hibernation.
    pub fn hibernate(&mut self, options: HibernateOptions) -> Result<()> {
        self.options = options;

        info!("Beginning hibernate");

        log_metric_event(HibernateEvent::SuspendAttempt);

        let res = self.hibernate_inner();

        // Now send any remaining logs and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);

        let hibermeta_mount = self.volume_manager.mount_hibermeta()?;

        // Replay the suspend (and maybe resume) logs to the syslogger.
        // replay logs first because they happened earlier.
        replay_logs(
            &hibermeta_mount,
            res.is_ok() && !self.options.dry_run, // push_resume_logs
            !self.options.dry_run,                // clear
        );

        match res {
            Ok(()) => {
                log_metric_event(HibernateEvent::ResumeSuccess);
                self.record_resume_time_metric(ResumeTimeMetric::RestoreSystem, &hibermeta_mount);
                self.record_resume_time_metric(ResumeTimeMetric::Total, &hibermeta_mount);
            },
            Err(_) => {
                log_metric_event(HibernateEvent::SuspendFailure);
            }
        };

        // Read the metrics files and send out the samples.
        read_and_send_metrics(&hibermeta_mount);

        res
    }

    /// Hibernates the system, and returns either upon failure to hibernate or
    /// after the system has resumed from a successful hibernation.
    fn hibernate_inner(&mut self) -> Result<()> {
        if !self.volume_manager.hiberimage_exists() {
            if has_user_logged_out() {
                info!(
                    "'hiberimage' does not exist (prior user logout), aborting hibernate attempt"
                );
                Self::log_suspend_abort(SuspendAbortReason::PriorUserLogout);
            } else {
                info!("'hiberimage' does not exist, aborting hibernate attempt");
                Self::log_suspend_abort(SuspendAbortReason::NoHiberimage);
            }

            return Err(HibernateError::NoHiberimageError().into());
        }

        // TODO(b/237089652): remove this when PinWeaver data is stored on a dedicated partition.
        if cryptohome::has_pin_weaver_credentials()
            .context("failed to check for PinWeaver credentials")?
        {
            info!("PinWeaver credentials exist, aborting hibernate attempt");
            Self::log_suspend_abort(SuspendAbortReason::PinWeaverCredentialsExist);
            return Err(HibernateError::PinWeaverCredentialsExist().into());
        }

        if !self.volume_manager.is_hiberimage_thickened()? {
            let free_thinpool_space = self.volume_manager.get_free_thinpool_space()?;
            // The max image size is half of the system RAM, add a bit of margin.
            if free_thinpool_space < (get_ram_size() as f64 * 0.75) as u64 {
                warn!(
                    "Not enough space ({} MB) in the thinpool for writing the hibernate image",
                    free_thinpool_space / (1024 * 1024)
                );

                Self::log_suspend_abort(SuspendAbortReason::InsufficientDiskSpace);
                return Err(HibernateError::InsufficientDiskSpaceError().into());
            }
        }

        // Don't hibernate if the update engine is up to something, as we would
        // not want to hibernate if upon reboot the other slot gets booted.
        // While an update is "pending reboot", the update engine might do
        // further checks for updates it can apply. So no state except idle is
        // safe.
        if is_update_in_progress()? {
            Self::log_suspend_abort(SuspendAbortReason::UpdateEngineActive);
            return Err(HibernateError::UpdateEngineBusyError()).context("Update engine is active");
        }

        debug!("Syncing filesystems");
        // This is safe because sync() does not modify memory.
        unsafe {
            libc::sync();
        }

        // Make sure hiberman memory isn't swapped out by the memory
        // tweaks or during the hibernation process.
        let _locked_memory = lock_process_memory()?;

        Self::tweak_memory_usage()?;

        self.suspend_system()
    }

    /// Inner helper function to actually take the snapshot, save it to disk,
    /// and shut down. Returns upon a failure to hibernate, or after a
    /// successful hibernation has resumed.
    fn suspend_system(&mut self) -> Result<()> {
        // Stop logging to syslog, and divert instead to a file since the
        // logging daemon's about to be frozen.
        let hibermeta_mount = self.volume_manager.setup_hibermeta_lv(true)?;
        let log_file = LogFile::new(HibernateStage::Suspend, true, &hibermeta_mount)?;

        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Read)?;
        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.flush(&hibermeta_mount)?;
        }

        mem::drop(log_file);
        drop(hibermeta_mount);

        self.volume_manager.thicken_hiberimage()?;

        // Make sure the thinpool has time to commit pending metadata changes
        // to disk. The thinpool workqueue does this every second.
        thread::sleep(Duration::from_millis(1100));

        // Drop the page cache to reduce the size of the hibernate image. The
        // pages can be loaded from storage as needed after resume.
        if let Err(e) = drop_pagecache() {
            error!("Failed to drop pagecache: {e}");
        }

        record_free_memory_metric();

        // Abort the hibernation if there is clearly not enough free memory for the snapshot.
        if !might_have_enough_free_memory_for_snapshot() {
            Self::log_suspend_abort(SuspendAbortReason::InsufficientFreeMemory);
            return Err(HibernateError::InsufficientMemoryAvailableError().into());
        }

        if let Err(e) = self.snapshot_and_save(frozen_userspace) {
            if let Some(HibernateError::SnapshotIoctlError(_, err)) = e.downcast_ref() {
                if *err == nix::Error::ENOMEM {
                    Self::log_suspend_abort(SuspendAbortReason::InsufficientFreeMemory);
                } else {
                    Self::log_suspend_abort(SuspendAbortReason::Other);

                    // Record the errno to learn more about the prevalent errors.
                    Self::log_suspend_abort_errno(*err);
                }
            }

            return Err(e);
        }

        Ok(())
    }

    /// Snapshot the system, write the result to disk, and power down. Returns
    /// upon failure to hibernate, or after a hibernated system has successfully
    /// resumed.
    fn snapshot_and_save(&mut self, mut frozen_userspace: FrozenUserspaceTicket) -> Result<()> {
        let block_path = path_to_stateful_block()?;
        let dry_run = self.options.dry_run;
        let snap_dev = frozen_userspace.as_mut();

        let timestamp_hibernated = UNIX_EPOCH.elapsed().unwrap_or(Duration::ZERO);

        // This is where the suspend path and resume path fork. On success,
        // both halves of these conditions execute, just at different times.
        if snap_dev.atomic_snapshot()? {
            // Suspend path. Everything after this point is invisible to the
            // hibernated kernel.

            let image_size = snap_dev.get_image_size()?;
            let pages_with_zeroes = get_number_of_dropped_pages_with_zeroes()?;
            // Briefly remount 'hibermeta' to write logs and metrics.
            let hibermeta_mount = self.volume_manager.mount_hibermeta()?;

            let log_file = LogFile::new(HibernateStage::Suspend, false, &hibermeta_mount)?;

            let start = Instant::now();

            if let Err(e) = snap_dev.transfer_block_device() {
                snap_dev.unfreeze_userspace()?;
                return Err(e);
            }

            let io_duration = start.elapsed();

            log_metric_event(HibernateEvent::SuspendSuccess);

            {
                let page_size = get_page_size() as u64;
                let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

                metrics_logger.metrics_send_io_sample(
                    "WriteHibernateImage",
                    image_size,
                    io_duration,
                );

                let pages_with_zeroes_percent = ((pages_with_zeroes * 100)
                    / ((image_size / page_size) + pages_with_zeroes))
                    as usize;
                metrics_logger.log_percent_metric(
                    "Platform.Hibernate.DroppedPagesWithZeroesPercent",
                    pages_with_zeroes_percent,
                );

                // Flush the metrics file before unmounting hibermeta. The metrics will be
                // sent on resume.
                metrics_logger.flush(&hibermeta_mount)?;
            }

            hibermeta_mount.write_hiberimage_size(image_size)?;

            // Set the hibernate cookie so the next boot knows to start in RO mode.
            info!("Setting hibernate cookie at {}", block_path);
            set_hibernate_cookie(Some(&block_path), HibernateCookieValue::ResumeReady)?;
            if dry_run {
                info!("Not powering off due to dry run");
            } else {
                info!("Powering off");
            }

            mem::drop(log_file);
            drop(hibermeta_mount);

            // Power the thing down.
            if !dry_run {
                if !self.options.reboot {
                    Self::power_off()?;
                    error!("Returned from power off");
                } else {
                    Self::reboot()?;
                    error!("Returned from reboot");
                }
            }
        } else {
            self.timestamp_resumed = Some(UNIX_EPOCH.elapsed().unwrap_or(Duration::ZERO));

            // This is the resume path. First, forcefully reset the logger, which is some
            // stale partial state that the suspend path ultimately flushed and closed.
            // Keep logs in memory for now.
            reset_log();
            redirect_log(HiberlogOut::BufferInMemory);

            info!("Resumed from hibernate");

            let timestamp_resumed = self.timestamp_resumed.unwrap();
            let time_hibernated = timestamp_resumed
                .checked_sub(timestamp_hibernated)
                .unwrap_or_else(|| -> Duration {
                    warn!(
                        "Hibernate timestamps are bogus: hibernate time: {:?}, resume time: {:?})",
                        timestamp_hibernated, timestamp_resumed
                    );
                    Duration::ZERO
                });

            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.log_duration_sample(
                "Platform.Hibernate.HibernateDuration",
                time_hibernated,
                DurationMetricUnit::Hours,
                8760, // 1 year
            );

            match get_kernel_restore_time() {
                Ok(restore_time) => {
                    metrics_logger.log_duration_sample(
                        "Platform.Hibernate.ResumeTime.KernelResume",
                        restore_time,
                        DurationMetricUnit::Milliseconds,
                        10000,
                    );
                }

                Err(e) => warn!("Failed to get kernel restore time: {e:?}"),
            };
        }

        // Unset the hibernate cookie.
        info!("Clearing hibernate cookie at {}", block_path);
        set_hibernate_cookie(Some(&block_path), HibernateCookieValue::NoResume)
            .context("Failed to clear hibernate cookie")
    }

    /// Apply tweaks to reduce memory usage.
    fn tweak_memory_usage() -> Result<()> {
        // Push all non-file backed reclaimable memory to zram.
        reclaim_all_processes().context("Failed to perform memory reclaim")?;

        if zram_is_writeback_enabled()? {
            Self::trigger_zram_writeback()?;
        }

        Ok(())
    }

    /// Trigger a zram writeback.
    fn trigger_zram_writeback() -> Result<()> {
        let on_disk_before = zram_get_bd_stats()?.bytes_on_disk;

        swap_zram_set_writeback_limit(ZRAM_WRITEBACK_LIMIT_MB_MAX)
            .context("Failed to set zram writeback limit")?;

        swap_zram_mark_idle(0).context("Failed to configure zram writeback")?;

        if let Err(e) = initiate_swap_zram_writeback(WritebackMode::Idle) {
            if nix::Error::last() != nix::Error::EAGAIN {
                return Err(e.context("Failed to initiate zram writeback"));
            }

            warn!(
                "The zram backing device is full, some zram data will be \
                   part of the hibernate image"
            );
        }

        let on_disk_now = zram_get_bd_stats()?.bytes_on_disk;
        let written = on_disk_now.saturating_sub(on_disk_before);

        debug!(
            "{}MB of zram written back before hibernate, total of {}MB on disk",
            written / (1024 * 1024),
            on_disk_now / (1024 * 1024)
        );

        // TODO: report metric(s)

        Ok(())
    }

    /// Record a resume time metric
    fn record_resume_time_metric(&self, metric: ResumeTimeMetric, hibermeta_mount: &ActiveMount) {
        if self.timestamp_resumed.is_none() {
            return;
        }

        let (ts_file_name, metric_name, max_value) = match metric {
            ResumeTimeMetric::RestoreSystem => ("restore_start.ts", "RestoreSystem", 10000),
            ResumeTimeMetric::Total => ("resume_start.ts", "Total", 30000),
        };

        let res = TimestampFile::new(hibermeta_mount).read_timestamp(ts_file_name);
        if let Err(e) = res {
            warn!("Failed to read {ts_file_name}: {e}");
            return;
        }

        let start = res.unwrap();
        let resume_done = self.timestamp_resumed.unwrap();
        let duration = resume_done
            .checked_sub(start)
            .unwrap_or_else(|| -> Duration {
                warn!(
                    "Resume timestamps are bogus: start: {:?}, resume done: {:?}",
                    start, resume_done
                );
                Duration::ZERO
            });

        let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

        metrics_logger.log_duration_sample(
            &format!("Platform.Hibernate.ResumeTime.{}", metric_name),
            duration,
            DurationMetricUnit::Milliseconds,
            max_value,
        );

        if metric == ResumeTimeMetric::Total {
            debug!(
                "Resume from hibernate took {}.{}.s",
                duration.as_secs(),
                duration.subsec_millis()
            );
        }
    }

    /// Utility function to power the system down immediately.
    fn power_off() -> Result<()> {
        // This is safe because the system either ceases to exist, or does
        // nothing to memory.
        unsafe {
            // On success, we shouldn't be executing, so the return code can be
            // ignored because we already know it's a failure.
            let _ = reboot(RB_POWER_OFF);
            Err(HibernateError::ShutdownError(nix::Error::last())).context("Failed to shut down")
        }
    }

    /// Utility function to reboot the system immediately.
    fn reboot() -> Result<()> {
        // This is safe because the system either ceases to exist, or does
        // nothing to memory.
        unsafe {
            // On success, we shouldn't be executing, so the return code can be
            // ignored because we already know it's a failure.
            let _ = reboot(RB_AUTOBOOT);
            Err(HibernateError::ShutdownError(nix::Error::last())).context("Failed to reboot")
        }
    }

    fn log_suspend_abort(reason: SuspendAbortReason) {
        let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

        metrics_logger.log_enum_metric(
            "Platform.Hibernate.Abort",
            reason as isize,
            SuspendAbortReason::Count as isize - 1,
        );
    }

    fn log_suspend_abort_errno(errno: Errno) {
        let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

        metrics_logger.log_linear_metric("Platform.Hibernate.Abort.Errno", errno as isize, 1, 200);
    }
}

/// Check whether the system might have enough free memory for creating the
/// hibernate snapshot. This check can give false positives (the system doesn't
/// actually have enough memory), but shouldn't give false negatives.
///
/// Generally half of the system RAM needs to be free to be able to store the
/// hibernate snapshot in memory. A kernel optimization which removes pages that
/// only contain zeros can shrink the image by up to 35%, so check whether at
/// least 65% of half of the RAM is  available. If the image has less than 35%
/// of pages with zeros then the creation of the snapshot will fail with -ENOMEM,
/// which is explicitly handled by hiberman.
fn might_have_enough_free_memory_for_snapshot() -> bool {
    let ram_size_mb = get_ram_size() / (1024 * 1024);
    let required_mb = ((ram_size_mb / 2) * 65) / 100;
    let free_mb = get_available_memory_mb() as u64;

    if free_mb < required_mb {
        info!(
            "not enough free memory ({}MB) for creating the hibernate snapshot (min: {}MB)",
            free_mb, required_mb
        );
        return false;
    }

    true
}

/// Logs a hibernate metric event.
fn log_metric_event(event: HibernateEvent) {
    let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
    metrics_logger.log_event(event);
}

/// Get the number of pages that were not included in the hibernate image
/// by the kernel because they only contain zeroes.
fn get_number_of_dropped_pages_with_zeroes() -> Result<u64> {
    // We can't limit the logs with something like "--since 1 minute ago" here,
    // it often causes dmesg to hang when it tries to open /etc/localtime,
    // probably due to a frozen filesystem or storage kernel thread.
    let output = checked_command_output(Command::new("/bin/dmesg").args(["-P"]))
        .context("Failed to execute 'dmesg'")?;

    // regular expression for extracting the number of pages with zeroes
    let re = Regex::new(r"Image created \(\d+ pages copied, (\d+) zero pages\)").unwrap();
    let mut value: Option<u64> = None;

    for line in output.stdout.lines() {
        if line.is_err() {
            continue;
        }

        let line = line.unwrap();

        // limit regex matching to eligible lines.
        if !line.contains("Image created") {
            continue;
        }

        let cap = re.captures(&line);
        if let Some(cap) = cap {
            value = Some(cap[1].parse::<u64>()?);
        }
    }

    value.ok_or(anyhow!(
        "Could not determine number of dropped pages with only zeroes"
    ))
}

/// Drop the pagecache.
fn drop_pagecache() -> Result<()> {
    let mut f = File::options()
        .write(true)
        .open(DROP_CACHES_ATTR_PATH)
        .context("Failed to open {DROP_CACHES_ATTR_PATH}")?;
    Ok(f.write_all("1".as_bytes())?)
}

/// Record the amount of free memory as a metric.
pub fn record_free_memory_metric() {
    let available_mb = get_available_memory_mb();

    debug!("System has {available_mb} MB of free memory");

    let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

    metrics_logger.log_metric(
        "Platform.Hibernate.MemoryAvailable",
        available_mb as isize,
        0,
        32768,
        50,
    );
}
