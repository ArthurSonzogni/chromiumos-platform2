// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements hibernate suspend functionality.

use std::thread;
use std::time::Duration;
use std::time::Instant;
use std::time::UNIX_EPOCH;

use anyhow::Context;
use anyhow::Result;
use libc::reboot;
use libc::RB_AUTOBOOT;
use libc::RB_POWER_OFF;
use log::debug;
use log::error;
use log::info;
use log::warn;

use crate::cookie::set_hibernate_cookie;
use crate::cookie::HibernateCookieValue;
use crate::hiberlog;
use crate::hiberlog::redirect_log;
use crate::hiberlog::replay_logs;
use crate::hiberlog::reset_log;
use crate::hiberlog::HiberlogOut;
use crate::hiberutil::path_to_stateful_block;
use crate::hiberutil::prealloc_mem;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateOptions;
use crate::hiberutil::HibernateStage;
use crate::hiberutil::TimestampFile;
use crate::metrics::log_hibernate_attempt;
use crate::metrics::read_and_send_metrics;
use crate::metrics::DurationMetricUnit;
use crate::metrics::METRICS_LOGGER;
use crate::snapdev::FrozenUserspaceTicket;
use crate::snapdev::SnapshotDevice;
use crate::snapdev::SnapshotMode;
use crate::update_engine::is_update_engine_idle;
use crate::volume::VolumeManager;

/// The SuspendConductor weaves a delicate baton to guide us through the
/// symphony of hibernation.
pub struct SuspendConductor {
    options: HibernateOptions,
    volume_manager: VolumeManager,
    timestamp_resumed: Option<Duration>,
}

impl SuspendConductor {
    /// Create a new SuspendConductor in preparation for imminent hibernation.
    pub fn new() -> Result<Self> {
        Ok(SuspendConductor {
            options: Default::default(),
            volume_manager: VolumeManager::new()?,
            timestamp_resumed: None,
        })
    }

    /// Public entry point that hibernates the system, and returns either upon
    /// failure to hibernate or after the system has resumed from a successful
    /// hibernation.
    pub fn hibernate(&mut self, options: HibernateOptions) -> Result<()> {
        self.options = options;

        info!("Beginning hibernate");

        self.volume_manager.setup_hibermeta_lv(true)?;

        if let Err(e) = log_hibernate_attempt() {
            warn!("Failed to log hibernate attempt: \n {}", e);
        }

        // Don't hibernate if the update engine is up to something, as we would
        // not want to hibernate if upon reboot the other slot gets booted.
        // While an update is "pending reboot", the update engine might do
        // further checks for updates it can apply. So no state except idle is
        // safe.
        if !is_update_engine_idle()? {
            return Err(HibernateError::UpdateEngineBusyError()).context("Update engine is active");
        }

        // Stop logging to syslog, and divert instead to a file since the
        // logging daemon's about to be frozen.
        let log_file_path = hiberlog::LogFile::get_path(HibernateStage::Suspend);
        let log_file = hiberlog::LogFile::create(log_file_path)?;
        redirect_log(HiberlogOut::File(Box::new(log_file)));

        debug!("Syncing filesystems");
        // This is safe because sync() does not modify memory.
        unsafe {
            libc::sync();
        }

        prealloc_mem().context("Failed to preallocate memory for hibernate")?;

        let result = self.suspend_system();

        self.volume_manager.mount_hibermeta()?;

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

        self.volume_manager.unmount_hibermeta()?;

        result
    }

    /// Inner helper function to actually take the snapshot, save it to disk,
    /// and shut down. Returns upon a failure to hibernate, or after a
    /// successful hibernation has resumed.
    fn suspend_system(&mut self) -> Result<()> {
        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Read)?;
        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.flush()?;
        }

        redirect_log(HiberlogOut::BufferInMemory);
        self.volume_manager.unmount_hibermeta()?;

        self.volume_manager.thicken_hiberimage()?;

        // Make sure the thinpool has time to commit pending metadata changes
        // to disk. The thinpool workqueue does this every second.
        thread::sleep(Duration::from_millis(1100));

        self.snapshot_and_save(frozen_userspace)
    }

    /// Snapshot the system, write the result to disk, and power down. Returns
    /// upon failure to hibernate, or after a hibernated system has successfully
    /// resumed.
    fn snapshot_and_save(&mut self, mut frozen_userspace: FrozenUserspaceTicket) -> Result<()> {
        let block_path = path_to_stateful_block()?;
        let dry_run = self.options.dry_run;
        let snap_dev = frozen_userspace.as_mut();

        // This is where the suspend path and resume path fork. On success,
        // both halves of these conditions execute, just at different times.
        if snap_dev.atomic_snapshot()? {
            // Suspend path. Everything after this point is invisible to the
            // hibernated kernel.

            // Briefly remount 'hibermeta' to write logs and metrics.
            self.volume_manager.mount_hibermeta()?;
            let log_file_path = hiberlog::LogFile::get_path(HibernateStage::Suspend);
            let log_file = hiberlog::LogFile::open(log_file_path)?;
            redirect_log(HiberlogOut::File(Box::new(log_file)));

            let start = Instant::now();

            if let Err(e) = snap_dev.transfer_block_device() {
                snap_dev.unfreeze_userspace()?;
                return Err(e);
            }

            let io_duration = start.elapsed();

            {
                let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

                metrics_logger.metrics_send_io_sample(
                    "WriteHibernateImage",
                    snap_dev.get_image_size()?,
                    io_duration,
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

            redirect_log(HiberlogOut::BufferInMemory);
            self.volume_manager.unmount_hibermeta()?;

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
            self.timestamp_resumed = Some(UNIX_EPOCH.elapsed().unwrap());

            // This is the resume path. First, forcefully reset the logger, which is some
            // stale partial state that the suspend path ultimately flushed and closed.
            // Keep logs in memory for now.
            reset_log();
            redirect_log(HiberlogOut::BufferInMemory);

            info!("Resumed from hibernate");
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
        let resume_time = self.timestamp_resumed.unwrap() - resume_start;

        debug!(
            "Resume from hibernate took {}.{}.s",
            resume_time.as_secs(),
            resume_time.subsec_millis()
        );

        let mut metrics_logger = METRICS_LOGGER.lock().unwrap();

        metrics_logger.log_duration_sample(
            "Platform.Hibernate.LoginToResumeReady",
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
            Err(HibernateError::ShutdownError(
                libchromeos::sys::Error::last(),
            ))
            .context("Failed to shut down")
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
            Err(HibernateError::ShutdownError(
                libchromeos::sys::Error::last(),
            ))
            .context("Failed to reboot")
        }
    }
}
