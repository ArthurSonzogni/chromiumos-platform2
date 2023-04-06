// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements hibernate suspend functionality.

use std::ffi::CString;
use std::mem::MaybeUninit;
use std::time::Instant;

use anyhow::Context;
use anyhow::Result;
use libc::reboot;
use libc::RB_POWER_OFF;
use libchromeos::sys::syscall;
use log::debug;
use log::error;
use log::info;
use log::warn;

use crate::cookie::set_hibernate_cookie;
use crate::cookie::HibernateCookieValue;
use crate::files::does_hiberfile_exist;
use crate::files::STATEFUL_DIR;
use crate::hiberlog;
use crate::hiberlog::redirect_log;
use crate::hiberlog::replay_logs;
use crate::hiberlog::reset_log;
use crate::hiberlog::HiberlogOut;
use crate::hiberutil::log_duration;
use crate::hiberutil::path_to_stateful_block;
use crate::hiberutil::prealloc_mem;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateOptions;
use crate::hiberutil::HibernateStage;
use crate::lvm::is_lvm_system;
use crate::metrics::log_hibernate_attempt;
use crate::metrics::read_and_send_metrics;
use crate::metrics::MetricsFile;
use crate::metrics::MetricsLogger;
use crate::snapdev::FrozenUserspaceTicket;
use crate::snapdev::SnapshotDevice;
use crate::snapdev::SnapshotMode;
use crate::update_engine::is_update_engine_idle;
use crate::volume::VolumeManager;

/// Define the swappiness value we'll set during hibernation.
const SUSPEND_SWAPPINESS: i32 = 100;
/// Define how low stateful free space must be as a percentage before we clean
/// up the hiberfile after each hibernate.
const LOW_DISK_FREE_THRESHOLD_PERCENT: u64 = 10;

/// The SuspendConductor weaves a delicate baton to guide us through the
/// symphony of hibernation.
pub struct SuspendConductor {
    options: HibernateOptions,
    metrics: MetricsLogger,
    volume_manager: VolumeManager,
}

impl SuspendConductor {
    /// Create a new SuspendConductor in preparation for imminent hibernation.
    pub fn new() -> Result<Self> {
        Ok(SuspendConductor {
            options: Default::default(),
            metrics: MetricsLogger::new()?,
            volume_manager: VolumeManager::new()?,
        })
    }

    /// Public entry point that hibernates the system, and returns either upon
    /// failure to hibernate or after the system has resumed from a successful
    /// hibernation.
    pub fn hibernate(&mut self, options: HibernateOptions) -> Result<()> {
        info!("Beginning hibernate");
        let start = Instant::now();

        self.volume_manager.setup_hibermeta_lv(true)?;

        if let Err(e) = log_hibernate_attempt() {
            warn!("Failed to log hibernate attempt: \n {}", e);
        }

        let is_lvm = is_lvm_system()?;
        let files_exist = does_hiberfile_exist();

        let metrics_file_path = MetricsFile::get_path(HibernateStage::Resume);
        let metrics_file = MetricsFile::create(metrics_file_path)?;
        self.metrics.file = Some(metrics_file);

        let action_string = format!(
            "Set up {}hibernate files on {}LVM system",
            if files_exist { "" } else { "new " },
            if is_lvm { "" } else { "non-" }
        );
        let duration = start.elapsed();
        log_duration(&action_string, duration);
        self.metrics
            .metrics_send_duration_sample("SetupLVMFiles", duration, 30);

        self.options = options;

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

        prealloc_mem(&mut self.metrics).context("Failed to preallocate memory for hibernate")?;

        let result = self.suspend_system();

        self.volume_manager.mount_hibermeta()?;

        // Now send any remaining logs and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);
        // Replay logs first because they happened earlier.
        replay_logs(
            result.is_ok() && !self.options.dry_run,
            !self.options.dry_run,
        );
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

        self.metrics.flush()?;
        self.metrics.file = None;
        redirect_log(HiberlogOut::BufferInMemory);
        self.volume_manager.unmount_hibermeta()?;

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
            // Briefly remount 'hibermeta' to write logs and metrics.
            self.volume_manager.mount_hibermeta()?;
            let log_file_path = hiberlog::LogFile::get_path(HibernateStage::Suspend);
            let log_file = hiberlog::LogFile::open(log_file_path)?;
            redirect_log(HiberlogOut::File(Box::new(log_file)));

            // Suspend path. Everything after this point is invisible to the
            // hibernated kernel.
            if let Err(e) = snap_dev.transfer_block_device() {
                snap_dev.unfreeze_userspace()?;
                return Err(e);
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
                Self::power_off()?;
                error!("Returned from power off");
            }
        } else {
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

    /// Clean up the hibernate files, releasing that space back to other usermode apps.
    fn delete_data_if_disk_full(&mut self, fs_stats: libc::statvfs) {
        let free_percent = fs_stats.f_bfree * 100 / fs_stats.f_blocks;
        if free_percent < LOW_DISK_FREE_THRESHOLD_PERCENT {
            debug!("Freeing hiberdata: FS is only {}% free", free_percent);
            // TODO: Unlink hiberfile.
        } else {
            debug!("Not freeing hiberfile: FS is {}% free", free_percent);
        }
    }

    /// Utility function to get the current stateful file system usage.
    fn get_fs_stats() -> Result<libc::statvfs> {
        let path = CString::new(STATEFUL_DIR).unwrap();
        let mut stats: MaybeUninit<libc::statvfs> = MaybeUninit::zeroed();

        // This is safe because only stats is modified.
        syscall!(unsafe { libc::statvfs(path.as_ptr(), stats.as_mut_ptr()) })?;

        // Safe because the syscall just initialized it, and we just verified
        // the return was successful.
        unsafe { Ok(stats.assume_init()) }
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
}
