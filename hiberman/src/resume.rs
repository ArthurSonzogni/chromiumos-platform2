// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements hibernate resume functionality.

use std::fs;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Read;

use anyhow::Context;
use anyhow::Result;
use log::debug;
use log::error;
use log::info;
use log::warn;

use crate::cookie::cookie_description;
use crate::cookie::get_hibernate_cookie;
use crate::cookie::set_hibernate_cookie;
use crate::cookie::HibernateCookieValue;
use crate::dbus::HibernateKey;
use crate::device_mapper::DeviceMapper;
use crate::files::remove_resume_in_progress_file;
use crate::hiberlog;
use crate::hiberlog::redirect_log;
use crate::hiberlog::replay_logs;
use crate::hiberlog::HiberlogOut;
use crate::hiberutil::lock_process_memory;
use crate::hiberutil::path_to_stateful_block;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateStage;
use crate::hiberutil::ResumeOptions;
use crate::lvm::activate_physical_lv;
use crate::metrics::read_and_send_metrics;
use crate::metrics::MetricsFile;
use crate::metrics::MetricsLogger;
use crate::powerd::PowerdPendingResume;
use crate::snapdev::FrozenUserspaceTicket;
use crate::snapdev::SnapshotDevice;
use crate::snapdev::SnapshotMode;
use crate::volume::PendingStatefulMerge;
use crate::volume::VolumeManager;

/// The expected size of a TPM key.
const TPM_SEED_SIZE: usize = 32;
/// The path where the TPM key will be stored.
const TPM_SEED_FILE: &str = "/run/hiberman/tpm_seed";

/// The ResumeConductor orchestrates the various individual instruments that
/// work in concert to resume the system from hibernation.
pub struct ResumeConductor {
    options: ResumeOptions,
    metrics_logger: MetricsLogger,
    stateful_block_path: String,
}

impl ResumeConductor {
    /// Create a new resume conductor in prepration for an impending resume.
    pub fn new() -> Result<Self> {
        Ok(ResumeConductor {
            options: Default::default(),
            metrics_logger: MetricsLogger::new()?,
            stateful_block_path: path_to_stateful_block()?,
        })
    }

    /// Public entry point into the resume process. In the case of a successful
    /// resume, this does not return, as the resume image is running instead. In
    /// the case of resume failure, an error is returned.
    pub fn resume(&mut self, options: ResumeOptions) -> Result<()> {
        info!("Beginning resume");
        // Ensure the persistent version of the stateful block device is available.
        let _rw_stateful_lv = activate_physical_lv("unencrypted")?;
        self.options = options;
        // Create a variable that will merge the stateful snapshots when this
        // function returns one way or another.
        let mut volume_manager = VolumeManager::new()?;
        let mut pending_merge = PendingStatefulMerge::new(&mut volume_manager)?;
        // Start keeping logs in memory, anticipating success.
        redirect_log(HiberlogOut::BufferInMemory);

        let result = self.resume_inner(&mut pending_merge);

        // If we get here resuming from hibernate failed and we continue to
        // run the bootstrap system.

        // Move pending and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);

        // Mount hibermeta for access to logs and metrics. Create it if it doesn't exist yet.
        VolumeManager::new()?.setup_hibermeta_lv(true)?;

        // Now replay earlier logs. Don't wipe the logs out if this is just a dry
        // run.
        replay_logs(true, !self.options.dry_run);
        // Remove the resume_in_progress token file if it exists.
        remove_resume_in_progress_file();
        // Since resume_inner() returned, we are no longer in a viable resume
        // path. Drop the pending merge object, causing the stateful
        // dm-snapshots to merge with their origins.
        drop(pending_merge);
        // Read the metrics files to send out samples.
        read_and_send_metrics();

        VolumeManager::new()?.unmount_hibermeta()?;

        result
    }

    /// Helper function to perform the meat of the resume action now that the
    /// logging is routed.
    fn resume_inner(&mut self, pending_merge: &mut PendingStatefulMerge) -> Result<()> {
        if let Err(e) = self.decide_to_resume(pending_merge) {
            // No resume from hibernate

            let mut volume_manager = VolumeManager::new()?;

            // Make sure the thinpool is writable before removing the LVs.
            volume_manager.make_thinpool_rw()?;

            // Remove hiberimage volumes if they exist to release allocated
            // storage to the thinpool.
            volume_manager.teardown_hiberimage()?;

            // Set up the snapshot device for future hibernates
            self.setup_snapshot_device(true)?;

            return Err(e);
        }

        VolumeManager::new()?.setup_hibermeta_lv(false)?;

        let metrics_file_path = MetricsFile::get_path(HibernateStage::Resume);
        let metrics_file = MetricsFile::create(metrics_file_path)?;
        self.metrics_logger.file = Some(metrics_file);

        // Set up the snapshot device for resuming
        self.setup_snapshot_device(false)?;

        debug!("Opening hiberimage");
        let hiber_image_file = OpenOptions::new()
            .read(true)
            .create(false)
            .open(DeviceMapper::device_path(VolumeManager::HIBERIMAGE))
            .unwrap();
        let _locked_memory = lock_process_memory()?;
        self.resume_system(hiber_image_file)
    }

    /// Helper function to evaluate the hibernate cookie and decide whether or
    /// not to continue with resume.
    fn decide_to_resume(&self, pending_merge: &mut PendingStatefulMerge) -> Result<()> {
        // If the cookie left by hibernate and updated by resume-init doesn't
        // indicate readiness, skip the resume unless testing manually.
        let cookie = get_hibernate_cookie(Some(&self.stateful_block_path))
            .context("Failed to get hibernate cookie")?;
        if cookie != HibernateCookieValue::ResumeInProgress {
            let description = cookie_description(&cookie);
            if self.options.dry_run {
                info!(
                    "Hibernate cookie was {}, continuing anyway due to --dry-run",
                    description
                );
            } else {
                if cookie == HibernateCookieValue::NoResume {
                    info!("No resume pending");
                } else {
                    warn!("Hibernate cookie was {}, abandoning resume", description);
                }

                // If the cookie indicates an emergency reboot, clear it back to
                // nothing, as the problem was logged.
                if cookie == HibernateCookieValue::EmergencyReboot {
                    set_hibernate_cookie(
                        Some(&self.stateful_block_path),
                        HibernateCookieValue::NoResume,
                    )
                    .context("Failed to clear emergency reboot cookie")?;
                    // Resume-init doesn't activate the hibermeta LV in order to
                    // minimize failures in the critical path. Activate it now
                    // so logs can be replayed.
                    pending_merge.volume_manager.setup_hibermeta_lv(false)?;
                }

                return Err(HibernateError::CookieError(format!(
                    "Cookie was {}, abandoning resume",
                    description
                )))
                .context("Aborting resume due to cookie");
            }
        }

        Ok(())
    }

    /// Inner helper function to read the resume image and launch it.
    fn resume_system(&mut self, hiber_image_file: File) -> Result<()> {
        let log_file_path = hiberlog::LogFile::get_path(HibernateStage::Resume);
        let log_file = hiberlog::LogFile::create(log_file_path)?;
        // Start logging to the resume logger.
        redirect_log(HiberlogOut::File(Box::new(log_file)));

        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Write)?;

        snap_dev.write_image(hiber_image_file)?;

        // Let other daemons know it's the end of the world.
        let _powerd_resume = PowerdPendingResume::new(&mut self.metrics_logger)
            .context("Failed to call powerd for imminent resume")?;
        // Write a tombstone indicating we got basically all the way through to
        // attempting the resume. Both the current value (ResumeInProgress) and
        // this ResumeAborting value cause a reboot-after-crash to do the right
        // thing.
        set_hibernate_cookie(
            Some(&self.stateful_block_path),
            HibernateCookieValue::ResumeAborting,
        )
        .context("Failed to set hibernate cookie to ResumeAborting")?;

        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;

        // Close the metrics file before unmounting 'hibermeta'.
        self.metrics_logger.flush()?;
        self.metrics_logger.file = None;

        // Keep logs in memory for now.
        redirect_log(HiberlogOut::BufferInMemory);

        // TODO: creating several instances of the volume manager is ugly.
        // Should it be a ref-counted singleton?
        VolumeManager::new()?.unmount_hibermeta()?;

        if self.options.dry_run {
            info!("Not launching resume image: in a dry run.");

            Ok(())
        } else {
            self.launch_resume_image(frozen_userspace)
        }
    }

    /// Helper to set up the 'hiberimage' DM device and configuring it as
    /// snapshot device for hibernate.
    ///
    /// # Arguments
    ///
    /// * `new_hiberimage` - Indicates whether to create a new hiberimage or
    ///                      use an existing one (for resuming).
    fn setup_snapshot_device(&self, new_hiberimage: bool) -> Result<()> {
        // Load the TPM derived key.
        let tpm_key = self.get_tpm_derived_integrity_key()?;

        // TODO: pass actual USER DERIVED KEY!
        VolumeManager::new()?.setup_hiberimage(&tpm_key, &tpm_key, new_hiberimage)?;

        SnapshotDevice::new(SnapshotMode::Read)?
            .set_block_device(&DeviceMapper::device_path(VolumeManager::HIBERIMAGE))
    }

    fn get_tpm_derived_integrity_key(&self) -> Result<HibernateKey> {
        let mut f = File::open(TPM_SEED_FILE)?;

        // Now that we have the file open, immediately unlink it.
        fs::remove_file(TPM_SEED_FILE)?;

        let mut buf = Vec::new();
        f.read_to_end(&mut buf)?;
        if buf.len() != TPM_SEED_SIZE {
            return Err(HibernateError::KeyRetrievalError()).context("Incorrect size for tpm_seed");
        }

        Ok(HibernateKey::new(buf))
    }

    /// Jump into the already-loaded resume image. The PendingResumeCall isn't
    /// actually used, but is received to enforce the lifetime of the object.
    /// This prevents bugs where it accidentally gets dropped by the caller too
    /// soon, allowing normal boot to proceed while resume is also in progress.
    fn launch_resume_image(&mut self, mut frozen_userspace: FrozenUserspaceTicket) -> Result<()> {
        // Jump into the restore image. This resumes execution in the lower
        // portion of suspend_system() on success. Flush and stop the logging
        // before control is lost.
        info!("Launching resume image");
        let snap_dev = frozen_userspace.as_mut();
        let result = snap_dev.atomic_restore();
        error!("Resume failed");
        result
    }
}
