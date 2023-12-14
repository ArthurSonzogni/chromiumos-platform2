// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements hibernate resume functionality.

use std::cmp;
use std::fs;
use std::fs::File;
use std::io::Read;
use std::io::Write;
use std::mem;
use std::os::unix::io::AsRawFd;
use std::ptr;
use std::thread;
use std::time::Duration;
use std::time::Instant;
use std::time::UNIX_EPOCH;

use anyhow::anyhow;
use anyhow::Context;
use anyhow::Result;
use libchromeos::secure_blob::SecureBlob;
use log::debug;
use log::error;
use log::info;
use log::warn;

use crate::cookie::cookie_description;
use crate::cookie::get_hibernate_cookie;
use crate::cookie::set_hibernate_cookie;
use crate::cookie::HibernateCookieValue;
use crate::cryptohome;
use crate::device_mapper::DeviceMapper;
use crate::files::remove_resume_in_progress_file;
use crate::files::HIBERNATING_USER_FILE;
use crate::hiberlog::redirect_log;
use crate::hiberlog::replay_logs;
use crate::hiberlog::HiberlogOut;
use crate::hiberlog::LogRedirectGuard;
use crate::hiberutil::get_ram_size;
use crate::hiberutil::lock_process_memory;
use crate::hiberutil::path_to_stateful_block;
use crate::hiberutil::read_hiberimage_size;
use crate::hiberutil::sanitize_username;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateStage;
use crate::hiberutil::ResumeOptions;
use crate::hiberutil::TimestampFile;
use crate::lvm::activate_physical_lv;
use crate::metrics::read_and_send_metrics;
use crate::metrics::DurationMetricUnit;
use crate::metrics::HibernateEvent;
use crate::metrics::METRICS_LOGGER;
use crate::powerd::PowerdPendingResume;
use crate::resume_dbus::DBusEvent;
use crate::resume_dbus::DBusServer;
use crate::snapdev::FrozenUserspaceTicket;
use crate::snapdev::SnapshotDevice;
use crate::snapdev::SnapshotMode;
use crate::volume::ActiveMount;
use crate::volume::PendingStatefulMerge;
use crate::volume::VolumeManager;
use crate::volume::VOLUME_MANAGER;

/// The expected size of a TPM key.
const TPM_SEED_SIZE: usize = 32;
/// The path where the TPM key will be stored.
const TPM_SEED_FILE: &str = "/run/hiberman/tpm_seed";

/// The ResumeConductor orchestrates the various individual instruments that
/// work in concert to resume the system from hibernation.
pub struct ResumeConductor {
    options: ResumeOptions,
    stateful_block_path: String,
    current_user: Option<String>,
    tried_to_resume: bool,
    timestamp_start: Duration,
    preload_thread: Option<thread::JoinHandle<Result<()>>>,
}

impl ResumeConductor {
    /// Create a new resume conductor in prepration for an impending resume.
    pub fn new() -> Result<Self> {
        Ok(ResumeConductor {
            options: Default::default(),
            stateful_block_path: path_to_stateful_block()?,
            current_user: None,
            tried_to_resume: false,
            timestamp_start: Duration::ZERO,
            preload_thread: None,
        })
    }

    /// Public entry point into the resume process. In the case of a successful
    /// resume, this does not return, as the resume image is running instead. In
    /// the case of resume failure, an error is returned.
    pub fn resume(&mut self, options: ResumeOptions) -> Result<()> {
        if self.is_resume_pending()? {
            info!("Preparing for resume from hibernate");
        } else {
            info!("Setting the system up for future hibernation");
        }

        // Ensure the persistent version of the stateful block device is available.
        let _rw_stateful_lv = activate_physical_lv("unencrypted")?;
        self.options = options;
        // Create a variable that will merge the stateful snapshots when this
        // function returns one way or another.
        let pending_merge = PendingStatefulMerge::new()?;
        // Start keeping logs in memory, anticipating success.
        redirect_log(HiberlogOut::BufferInMemory);

        let result = self.resume_inner();

        // If we get here we are not resuming from hibernate and continue to
        // run the bootstrap system.

        if self.tried_to_resume {
            // We tried to resume, but did not succeed.
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.log_event(HibernateEvent::ResumeFailure);
        }

        // Move pending and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);

        // Mount hibermeta for access to logs and metrics. Create it if it doesn't exist yet.
        let _hibermeta_mount = {
            let volume_manager = VOLUME_MANAGER.read().unwrap();
            let res = volume_manager.setup_hibermeta_lv(true);
            match res {
                Ok(mount) => mount,
                Err(e) => {
                    if result.is_ok() {
                        return Err(e);
                    } else {
                        error!("{e:?}");
                        // Return the error returned by resume_inner().
                        return result;
                    }
                }
            }
        };

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

        result
    }

    /// Helper function to perform the meat of the resume action now that the
    /// logging is routed.
    fn resume_inner(&mut self) -> Result<()> {
        let mut dbus_server = DBusServer::new();

        if self.is_resume_pending()? {
            self.preload_hiberimage_data()?;
        }

        // Wait for the user to authenticate or a message that hibernate is
        // not supported.
        let user_key = match dbus_server.wait_for_event()? {
            DBusEvent::UserAuthenticated {
                account_id,
                session_id,
            } => {
                debug!("User authentication completed");
                self.current_user = Some(sanitize_username(&account_id)?);
                cryptohome::get_user_key_for_session(&session_id)?
            }

            DBusEvent::AbortRequest { reason } => {
                info!("hibernate is not available: {reason}");
                return Err(HibernateError::HibernateNotSupportedError(reason).into());
            }
        };

        if let Err(e) = self.decide_to_resume() {
            // No resume from hibernate

            if let Some(preload_thread) = self.preload_thread.take() {
                // Wait for the image data preload thread to finish.
                if let Err(e) = preload_thread.join() {
                    error!("preloading image data failed: {e:?}");
                }
            }

            let volume_manager = VOLUME_MANAGER.read().unwrap();

            // Make sure the thinpool is writable before removing the LVs.
            volume_manager.make_thinpool_rw()?;

            // Remove hiberimage volumes if they exist to release allocated
            // storage to the thinpool.
            volume_manager.teardown_hiberimage()?;

            // Set up the snapshot device for future hibernates
            self.setup_snapshot_device(true, user_key)?;

            volume_manager.lockdown_hiberimage()?;

            // Record the account id of the user who might be hibernating in the future.
            // This is done at login instead of at hibernate time because obtaining the
            // account id at hibernate time would be involved (b/308631058). This can be
            // changed if hiberman ever becomes a daemon.
            self.record_hibernating_user()?;

            return Err(e);
        }

        info!("Starting resume from hibernate");

        self.timestamp_start = UNIX_EPOCH.elapsed().unwrap_or(Duration::ZERO);

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.log_event(HibernateEvent::ResumeAttempt);
        }

        let volume_manager = VOLUME_MANAGER.read().unwrap();
        let hibermeta_mount = volume_manager.setup_hibermeta_lv(false)?;

        // Set up the snapshot device for resuming
        self.setup_snapshot_device(false, user_key)?;

        volume_manager.lockdown_hiberimage()?;

        let _locked_memory = lock_process_memory()?;
        self.resume_system(hibermeta_mount)
    }

    /// Helper function to evaluate the hibernate cookie and decide whether or
    /// not to continue with resume.
    fn decide_to_resume(&mut self) -> Result<()> {
        // If the cookie left by hibernate and updated by resume-init doesn't
        // indicate readiness, skip the resume unless testing manually.
        let cookie = get_hibernate_cookie(Some(&self.stateful_block_path))
            .context("Failed to get hibernate cookie")?;
        let description = cookie_description(&cookie);

        if cookie == HibernateCookieValue::ResumeInProgress || self.options.dry_run {
            if cookie == HibernateCookieValue::ResumeInProgress {
                let hibernating_user = Self::get_hibernating_user()?;
                if self.current_user != Some(hibernating_user) {
                    info!("Skipping resume: the current user is not the one who hibernated");

                    let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
                    metrics_logger.log_event(HibernateEvent::ResumeSkippedUserMismatch);

                    set_hibernate_cookie(
                        Some(&self.stateful_block_path),
                        HibernateCookieValue::NoResume,
                    )
                    .context("Failed to reset cookie")?;

                    return Err(HibernateError::UserMismatchError().into());
                }

                self.tried_to_resume = true;
            } else {
                info!(
                    "Hibernate cookie was {}, continuing anyway due to --dry-run",
                    description
                );
            }

            return Ok(());
        } else if cookie == HibernateCookieValue::NoResume {
            info!("No resume pending");

            return Err(HibernateError::CookieError("No resume pending".to_string()).into());
        }

        warn!("Hibernate cookie was {}, abandoning resume", description);

        // If the cookie indicates an emergency reboot, clear it back to
        // nothing, as the problem was logged.
        if cookie == HibernateCookieValue::EmergencyReboot {
            set_hibernate_cookie(
                Some(&self.stateful_block_path),
                HibernateCookieValue::NoResume,
            )
            .context("Failed to clear emergency reboot cookie")?;
        }

        Err(HibernateError::CookieError(format!(
            "Cookie was {}, abandoning resume",
            description
        )))
        .context("Aborting resume due to cookie")
    }

    /// Inner helper function to read the resume image and launch it.
    fn resume_system(&mut self, mut hibermeta_mount: ActiveMount) -> Result<()> {
        // Start logging to the resume logger.
        let redirect_guard = LogRedirectGuard::new(HibernateStage::Resume, true)?;

        // Let other daemons know it's the end of the world.
        let mut powerd_resume =
            PowerdPendingResume::new().context("Failed to call powerd for imminent resume")?;

        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Write)?;

        let start = Instant::now();
        // Load the snapshot image into the kernel
        let image_size = snap_dev.load_image()?;
        info!("Image loaded with size {}", image_size);

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.metrics_send_io_sample("ReadMainImage", image_size, start.elapsed());

            metrics_logger.log_duration_sample(
                "Platform.Hibernate.ResumeTime.LoadImage",
                start.elapsed(),
                DurationMetricUnit::Milliseconds,
                30000,
            );
        }

        powerd_resume.wait_for_hibernate_resume_ready()?;

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

        TimestampFile::record_timestamp("resume_start.ts", &self.timestamp_start)?;

        let resume_prep_done = UNIX_EPOCH.elapsed().unwrap_or(Duration::ZERO);
        let prep_time = resume_prep_done
            .checked_sub(self.timestamp_start)
            .unwrap_or(Duration::ZERO);
        debug!(
            "Preparation for resume from hibernate took {}.{}.s",
            prep_time.as_secs(),
            prep_time.subsec_millis()
        );
        // TODO: log metric?

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            // Flush the metrics file before unmounting 'hibermeta'.
            metrics_logger.flush()?;
        }

        // Keep logs in memory for now.
        mem::drop(redirect_guard);

        hibermeta_mount.unmount()?;

        // This is safe because sync() does not modify memory.
        unsafe {
            libc::sync();
        }

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
    /// * `completion_receiver` - Used to wait for resume completion.
    fn setup_snapshot_device(&mut self, new_hiberimage: bool, user_key: SecureBlob) -> Result<()> {
        // Load the TPM derived key.
        let tpm_key: SecureBlob = self.get_tpm_derived_integrity_key()?;
        let volume_manager = VOLUME_MANAGER.read().unwrap();

        volume_manager.setup_hiberimage(tpm_key.as_ref(), user_key.as_ref(), new_hiberimage)?;

        SnapshotDevice::new(SnapshotMode::Read)?
            .set_block_device(&DeviceMapper::device_path(VolumeManager::HIBERIMAGE)?)
    }

    fn get_tpm_derived_integrity_key(&self) -> Result<SecureBlob> {
        let mut f = File::open(TPM_SEED_FILE)?;

        // Now that we have the file open, immediately unlink it.
        fs::remove_file(TPM_SEED_FILE)?;

        let mut buf = Vec::new();
        f.read_to_end(&mut buf)?;
        if buf.len() != TPM_SEED_SIZE {
            return Err(HibernateError::KeyRetrievalError()).context("Incorrect size for tpm_seed");
        }

        Ok(SecureBlob::from(buf))
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

    /// Launches a thread that loads the encrypted data of the hibernate
    /// image into the page cache.
    fn preload_hiberimage_data(&mut self) -> Result<()> {
        let volume_manager = VOLUME_MANAGER.read().unwrap();

        volume_manager
            .setup_hiberimage_buffer_device()
            .context("Failed to set up buffer device for hiberimage")?;

        // briefly mount hibermeta to read the size of the hiberimage.
        let mut hibermeta_mount = volume_manager.setup_hibermeta_lv(true)?;
        let image_size = read_hiberimage_size()?;
        hibermeta_mount.unmount()?;

        let ram_size = get_ram_size();
        // Don't fill up more than 33% of the RAM.
        let preload_bytes = cmp::min(image_size, (ram_size * 33) / 100);

        self.preload_thread = Some(thread::spawn(move || preload_image_data(preload_bytes)));

        Ok(())
    }

    // Record the account id of the user that is hibernating.
    fn record_hibernating_user(&self) -> Result<()> {
        let mut f = File::create(HIBERNATING_USER_FILE.as_path()).context(format!(
            "failed to create {}",
            HIBERNATING_USER_FILE.display()
        ))?;
        // The account id has already been sanitized.
        f.write_all(self.current_user.as_ref().unwrap().as_bytes())
            .context(format!(
                "failed to write account id to {}",
                HIBERNATING_USER_FILE.display()
            ))?;

        Ok(())
    }

    /// Check whether a resume from hibernate is pending.
    fn is_resume_pending(&self) -> Result<bool> {
        let cookie = get_hibernate_cookie(Some(&self.stateful_block_path))
            .context("Failed to get hibernate cookie")?;

        Ok(cookie == HibernateCookieValue::ResumeInProgress)
    }

    fn get_hibernating_user() -> Result<String> {
        fs::read_to_string(HIBERNATING_USER_FILE.as_path()).context(format!(
            "failed to read {}",
            HIBERNATING_USER_FILE.display()
        ))
    }
}

/// Preloads encrypted hiberimage data from disk. The goal is to
/// populate the page cache with this data before the user logs in,
/// to accelerate reading of the decrypted image after the user
/// logged in.
///
/// This function must run a dedicated thread.
fn preload_image_data(num_bytes: u64) -> Result<()> {
    let block_dev = {
        let volume_manager = VOLUME_MANAGER.read().unwrap();

        match volume_manager.get_hiberimage_buffer_device()? {
            Some(block_dev) => block_dev,
            None => {
                return Err(anyhow!(
                    "Could not determine hiberimage buffer device for preloading"
                ))
            }
        }
    };

    let f = File::open(&block_dev).context(format!(
        "Failed to open hiberimage buffer device {}",
        block_dev.display()
    ))?;

    debug!(
        "preloading {}MB of encrypted image data",
        num_bytes / (1024 * 1024)
    );

    let mapping = unsafe {
        libc::mmap(
            ptr::null_mut(),
            num_bytes.try_into().unwrap(),
            libc::PROT_READ,
            libc::MAP_PRIVATE | libc::MAP_FILE | libc::MAP_POPULATE,
            f.as_raw_fd(),
            0,
        )
    };

    if mapping == libc::MAP_FAILED {
        error!("failed to mmap buffer device");
        return Err(nix::Error::last().into());
    }

    let _ = unsafe { libc::munmap(mapping, num_bytes.try_into().unwrap()) };

    Ok(())
}
