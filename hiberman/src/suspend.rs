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
use crate::files::HIBERNATING_USER_FILE;
use crate::hiberlog;
use crate::hiberlog::redirect_log;
use crate::hiberlog::redirect_log_to_file;
use crate::hiberlog::replay_logs;
use crate::hiberlog::reset_log;
use crate::hiberlog::HiberlogOut;
use crate::hiberlog::LogRedirectGuard;
use crate::hiberutil::checked_command_output;
use crate::hiberutil::get_account_id;
use crate::hiberutil::get_kernel_restore_time;
use crate::hiberutil::get_page_size;
use crate::hiberutil::get_ram_size;
use crate::hiberutil::has_user_logged_out;
use crate::hiberutil::intel_keylocker_enabled;
use crate::hiberutil::path_to_stateful_block;
use crate::hiberutil::prealloc_mem;
use crate::hiberutil::sanitize_username;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateOptions;
use crate::hiberutil::HibernateStage;
use crate::hiberutil::TimestampFile;
use crate::metrics::read_and_send_metrics;
use crate::metrics::DurationMetricUnit;
use crate::metrics::HibernateEvent;
use crate::metrics::METRICS_LOGGER;
use crate::snapdev::FrozenUserspaceTicket;
use crate::snapdev::SnapshotDevice;
use crate::snapdev::SnapshotMode;
use crate::update_engine::is_update_engine_idle;
use crate::volume::ActiveMount;
use crate::volume::VolumeManager;
use crate::volume::VOLUME_MANAGER;

const DROP_CACHES_ATTR_PATH: &str = "/proc/sys/vm/drop_caches";

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
    Count = 6,
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

        if let Err(e) = self.hibernate_inner() {
            let _hibermeta_mount = self.volume_manager.mount_hibermeta()?;

            log_metric_event(HibernateEvent::SuspendFailure);

            read_and_send_metrics();

            return Err(e);
        }

        Ok(())
    }

    /// Hibernates the system, and returns either upon failure to hibernate or
    /// after the system has resumed from a successful hibernation.
    fn hibernate_inner(&mut self) -> Result<()> {
        let hibermeta_mount = self.volume_manager.setup_hibermeta_lv(true)?;

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
        if !is_update_engine_idle()? {
            Self::log_suspend_abort(SuspendAbortReason::UpdateEngineActive);
            return Err(HibernateError::UpdateEngineBusyError()).context("Update engine is active");
        }

        Self::record_hibernating_user()?;

        // Stop logging to syslog, and divert instead to a file since the
        // logging daemon's about to be frozen.
        let log_file_path = hiberlog::LogFile::get_path(HibernateStage::Suspend);
        let log_file = hiberlog::LogFile::create(log_file_path)?;
        let redirect_guard = redirect_log_to_file(log_file);

        debug!("Syncing filesystems");
        // This is safe because sync() does not modify memory.
        unsafe {
            libc::sync();
        }

        prealloc_mem().context("Failed to preallocate memory for hibernate")?;

        let result = self.suspend_system(hibermeta_mount, redirect_guard);

        if result.is_ok() {
            log_metric_event(HibernateEvent::ResumeSuccess);
        } else {
            // SuspendFailure event is recorded in hibernate()
        }

        let _hibermeta_mount = self.volume_manager.mount_hibermeta()?;

        // Now send any remaining logs and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);
        // Replay logs first because they happened earlier.
        replay_logs(
            result.is_ok() && !self.options.dry_run,
            !self.options.dry_run,
        );

        self.record_total_resume_time();

        // Read the metrics files and send out the samples.
        read_and_send_metrics();

        result
    }

    /// Inner helper function to actually take the snapshot, save it to disk,
    /// and shut down. Returns upon a failure to hibernate, or after a
    /// successful hibernation has resumed.
    ///
    /// The order of the `hibermeta_mount` and `log_redirect_guard` parameters
    /// must not be changed!!!
    fn suspend_system(
        &mut self,
        mut hibermeta_mount: ActiveMount,
        log_redirect_guard: LogRedirectGuard,
    ) -> Result<()> {
        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Read)?;
        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.flush()?;
        }

        mem::drop(log_redirect_guard);
        hibermeta_mount.unmount()?;

        self.volume_manager.thicken_hiberimage()?;

        // Make sure the thinpool has time to commit pending metadata changes
        // to disk. The thinpool workqueue does this every second.
        thread::sleep(Duration::from_millis(1100));

        // Drop the page cache to reduce the size of the hibernate image. The
        // pages can be loaded from storage as needed after resume.
        if let Err(e) = drop_pagecache() {
            error!("Failed to drop pagecache: {e}");
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

            let pages_with_zeroes = get_number_of_dropped_pages_with_zeroes()?;
            // Briefly remount 'hibermeta' to write logs and metrics.
            let mut hibermeta_mount = self.volume_manager.mount_hibermeta()?;
            let log_file_path = hiberlog::LogFile::get_path(HibernateStage::Suspend);
            let log_file = hiberlog::LogFile::open(log_file_path)?;
            let redirect_guard = redirect_log_to_file(log_file);

            let start = Instant::now();

            if let Err(e) = snap_dev.transfer_block_device() {
                snap_dev.unfreeze_userspace()?;
                return Err(e);
            }

            let io_duration = start.elapsed();

            log_metric_event(HibernateEvent::SuspendSuccess);

            {
                let image_size = snap_dev.get_image_size()?;
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
                metrics_logger.flush()?;
            }

            // Set the hibernate cookie so the next boot knows to start in RO mode.
            info!("Setting hibernate cookie at {}", block_path);
            set_hibernate_cookie(Some(&block_path), HibernateCookieValue::ResumeReady)?;
            if dry_run {
                info!("Not powering off due to dry run");
            } else {
                info!("Powering off");
            }

            mem::drop(redirect_guard);
            hibermeta_mount.unmount()?;

            // Power the thing down.
            if !dry_run {
                if !self.options.reboot {
                    if intel_keylocker_enabled()? {
                        snap_dev.power_off_platform_mode()?;
                    } else {
                        Self::power_off()?;
                    }

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

    /// Record the total resume time.
    fn record_total_resume_time(&self) {
        if self.timestamp_resumed.is_none() {
            return;
        }

        let res = TimestampFile::read_timestamp("resume_start.ts");
        if let Err(e) = res {
            warn!("Failed to read resume start timestap: {e}");
            return;
        }

        let resume_start = res.unwrap();
        let resume_done = self.timestamp_resumed.unwrap();
        let resume_time = resume_done
            .checked_sub(resume_start)
            .unwrap_or_else(|| -> Duration {
                warn!(
                    "Resume timestamps are bogus: resume start: {:?}, resume done: {:?}",
                    resume_start, resume_done
                );
                Duration::ZERO
            });

        debug!(
            "Resume from hibernate took {}.{}.s",
            resume_time.as_secs(),
            resume_time.subsec_millis()
        );

        let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

        metrics_logger.log_duration_sample(
            "Platform.Hibernate.ResumeTime.Total",
            resume_time,
            DurationMetricUnit::Milliseconds,
            30000,
        );
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

    // Record the account id of the user that is hibernating.
    fn record_hibernating_user() -> Result<()> {
        let account_id = get_account_id()?;
        let obfuscated_account_id = sanitize_username(&account_id)?;

        let mut f = File::create(HIBERNATING_USER_FILE.as_path()).context(format!(
            "failed to create {}",
            HIBERNATING_USER_FILE.display()
        ))?;
        f.write_all(obfuscated_account_id.as_bytes())
            .context(format!(
                "failed to write account id to {}",
                HIBERNATING_USER_FILE.display()
            ))?;

        Ok(())
    }
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
    f.write_all("1".as_bytes())?;

    Ok(())
}
