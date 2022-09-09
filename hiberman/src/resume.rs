// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement hibernate resume functionality

use std::convert::TryInto;
use std::io::{Read, Write};
use std::time::{Duration, Instant};

use anyhow::{Context, Result};
use log::{debug, error, info, warn};
use zeroize::Zeroize;

use crate::cookie::{
    cookie_description, get_hibernate_cookie, set_hibernate_cookie, HibernateCookieValue,
};
use crate::crypto::{CryptoMode, CryptoReader};
use crate::dbus::{HiberDbusConnection, PendingResumeCall};
use crate::diskfile::{BouncedDiskFile, DiskFile};
use crate::files::{
    open_header_file, open_hiberfile, open_kernel_key_file, open_log_file, open_metafile,
    open_metrics_file,
};
use crate::hiberlog::{redirect_log, replay_logs, HiberlogFile, HiberlogOut};
use crate::hibermeta::{
    HibernateMetadata, META_FLAG_ENCRYPTED, META_FLAG_KERNEL_ENCRYPTED, META_FLAG_RESUME_FAILED,
    META_FLAG_RESUME_LAUNCHED, META_FLAG_RESUME_STARTED, META_FLAG_VALID, META_HASH_SIZE,
};
use crate::hiberutil::ResumeOptions;
use crate::hiberutil::{
    emit_upstart_event, get_page_size, lock_process_memory, log_duration, log_io_duration,
    path_to_stateful_block, HibernateError, BUFFER_PAGES,
};
use crate::imagemover::ImageMover;
use crate::keyman::HibernateKeyManager;
use crate::lvm::activate_physical_lv;
use crate::metrics::{read_and_send_metrics, MetricsFile, MetricsLogger, MetricsSample};
use crate::mmapbuf::MmapBuffer;
use crate::powerd::PowerdPendingResume;
use crate::preloader::ImagePreloader;
use crate::snapdev::{
    FrozenUserspaceTicket, SnapshotDevice, SnapshotMode, UswsuspKeyBlob, UswsuspUserKey,
};
use crate::splitter::ImageJoiner;
use crate::volume::{PendingStatefulMerge, VolumeManager};

// The maximum value expected for the GotSeed duration metric.
pub const SEED_WAIT_METRIC_CEILING: isize = 120;

/// The ResumeConductor orchestrates the various individual instruments that
/// work in concert to resume the system from hibernation.
pub struct ResumeConductor {
    metadata: HibernateMetadata,
    key_manager: HibernateKeyManager,
    options: ResumeOptions,
    metrics_logger: MetricsLogger,
    stateful_block_path: String,
    tpm_done_event_emitted: bool,
}

impl ResumeConductor {
    /// Create a new resume conductor in prepration for an impending resume.
    pub fn new() -> Result<Self> {
        Ok(ResumeConductor {
            metadata: HibernateMetadata::new()?,
            key_manager: HibernateKeyManager::new(),
            options: Default::default(),
            metrics_logger: MetricsLogger::new()?,
            stateful_block_path: path_to_stateful_block()?,
            tpm_done_event_emitted: false,
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
        let pending_merge = PendingStatefulMerge::new(&mut volume_manager)?;
        // Fire up the dbus server.
        let mut dbus_connection = HiberDbusConnection::new()?;
        dbus_connection.spawn_dbus_server()?;
        // Start keeping logs in memory, anticipating success.
        redirect_log(HiberlogOut::BufferInMemory);
        let result = self.resume_inner(&mut dbus_connection);
        // Move pending and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);
        // Now replay earlier logs. Don't wipe the logs out if this is just a dry
        // run.
        replay_logs(true, !self.options.dry_run);
        // Allow trunksd to start if not already done.
        self.emit_tpm_done_event()?;
        // Since resume_inner() returned, we are no longer in a viable resume
        // path. Drop the pending merge object, causing the stateful
        // dm-snapshots to merge with their origins.
        drop(pending_merge);
        // Read the metrics files to send out samples.
        read_and_send_metrics();
        // Unless the test keys are being used, wait for the key material from
        // cryptohome and save the public portion for a later hibernate.
        if !self.options.test_keys {
            let save_result = self.save_public_key(&mut dbus_connection);
            if let Err(e) = &save_result {
                warn!("Failed to save public key: {:?}", e);
            }

            result.and(save_result)
        } else {
            result
        }
    }

    /// Helper function to perform the meat of the resume action now that the
    /// logging is routed.
    fn resume_inner(&mut self, dbus_connection: &mut HiberDbusConnection) -> Result<()> {
        self.decide_to_resume()?;
        let mut meta_file = open_metafile()?;
        debug!("Loading metadata");
        let mut metadata = HibernateMetadata::load_from_reader(&mut meta_file)?;
        if (metadata.flags & META_FLAG_VALID) == 0 {
            return Err(HibernateError::MetadataError(
                "No valid hibernate image".to_string(),
            ))
            .context("Failed to resume from hibernate");
        }

        // Mark that resume was attempted on this image in case it's the last thing
        // we do! This also clears out the private metadata on disk, getting the
        // (encrypted) data key off of disk. If this is just a dry run, don't make
        // any changes.
        metadata.dont_save_private_data();
        if !self.options.dry_run {
            metadata.flags &= !META_FLAG_VALID;
            metadata.flags |= META_FLAG_RESUME_STARTED;
            debug!("Clearing valid flag on metadata: {:x}", metadata.flags);
            meta_file.rewind()?;
            metadata.write_to_disk(&mut meta_file)?;
        }

        let metrics_file = open_metrics_file(MetricsFile::Resume)?;
        self.metrics_logger.file = Some(metrics_file);

        debug!("Opening hiberfile");
        let hiber_file = open_hiberfile()?;
        let _locked_memory = lock_process_memory()?;
        let header_file = open_header_file()?;
        let kernel_key_file = open_kernel_key_file()?;
        self.metadata = metadata;
        self.resume_system(
            header_file,
            hiber_file,
            meta_file,
            kernel_key_file,
            dbus_connection,
        )
    }

    /// Helper function to evaluate the hibernate cookie and decide whether or
    /// not to continue with resume.
    fn decide_to_resume(&self) -> Result<()> {
        // If the cookie left by hibernate and updated by resume-init doesn't
        // indicate readiness, skip the resume unless testing manually.
        let cookie = get_hibernate_cookie(Some(&self.stateful_block_path))
            .context("Failed to get hibernate cookie")?;
        if cookie != HibernateCookieValue::ResumeInProgress {
            let description = cookie_description(cookie);
            if self.options.dry_run {
                info!(
                    "Hibernate cookie was {}, continuing anyway due to --dry-run",
                    description
                );
            } else {
                warn!("Hibernate cookie was {}, abandoning resume", description);
                return Err(HibernateError::CookieError(format!(
                    "Cookie was {}, abandoning resume",
                    description
                )))
                .context("Aborting resume due to cookie");
            }
        }

        Ok(())
    }

    fn load_kernel_key_blob(
        &mut self,
        snap_dev: &mut SnapshotDevice,
        key_file: &mut BouncedDiskFile,
    ) -> Result<()> {
        let mut blob_buf = [0u8; 4096];
        key_file
            .read_exact(&mut blob_buf)
            .context("Could not read kernel key blob")?;
        let kernel_key_blob = UswsuspKeyBlob::deserialize(&blob_buf);
        snap_dev
            .set_key_blob(&kernel_key_blob)
            .context("Failed to set kernel key blob")?;

        Ok(())
    }

    /// Inner helper function to read the resume image and launch it.
    fn resume_system(
        &mut self,
        header_file: DiskFile,
        hiber_file: DiskFile,
        meta_file: BouncedDiskFile,
        mut kernel_key_file: BouncedDiskFile,
        dbus_connection: &mut HiberDbusConnection,
    ) -> Result<()> {
        let mut log_file = open_log_file(HiberlogFile::Resume)?;
        // Don't allow the logfile to log as it creates a deadlock.
        log_file.set_logging(false);
        // Start logging to the resume logger.
        redirect_log(HiberlogOut::File(Box::new(log_file)));
        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Write)?;
        snap_dev.set_platform_mode(false)?;
        if (self.metadata.flags & META_FLAG_KERNEL_ENCRYPTED) != 0 {
            debug!("Loading kernel key blob");
            self.load_kernel_key_blob(&mut snap_dev, &mut kernel_key_file)
                .context("Failed to load kernel key blob")?;
        }

        // With the kernel key loaded, all the TPM work is complete.
        self.emit_tpm_done_event()?;

        // The pending resume call represents the object blocking the
        // ResumeFromHibernate dbus call from completing and letting the rest of
        // boot continue. This gets dropped at the end of the function, when
        // resume has either completed (and not returned) or failed (so this
        // boot can continue).
        let mut pending_resume_call =
            self.read_image(header_file, hiber_file, &mut snap_dev, dbus_connection)?;
        // Explicitly clear out the secret seed before resume is attempted, in
        // case resume never returns. Make sure to use a reference so that
        // ownership isn't dropped at the end of the if statement (see comment
        // above).
        if let Some(ref mut pending_resume_call) = pending_resume_call {
            pending_resume_call.secret_seed.zeroize();
        }

        // Also explicitly clear the private key from the key manager. The
        // public key is still needed so it can be saved.
        self.key_manager.clear_private_key()?;
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

        // Mark this as the time between when login completed and when resume
        // was attempted. This is as close as we can get to the actual resume
        // launch while still being able to log a metric.
        if let Some(ref mut pending_resume_call) = pending_resume_call {
            let duration = pending_resume_call.when.elapsed();
            // Choose a range that represents the reasonable values in which a
            // user might expect to wait for hibernate resume to finish, plus
            // some padding. Beyond this max, hibernate performance is
            // so unacceptable that the number itself doesn't matter.
            self.metrics_logger.log_metric(MetricsSample {
                name: "Platform.Hibernate.LoginToResumeReady",
                value: duration.as_millis() as isize,
                min: 0,
                max: 60000,
                buckets: 50,
            });

            info!(
                "Login to resume took {}.{:02}s",
                duration.as_secs(),
                duration.subsec_millis()
            );
        }

        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;
        if let Err(e) = self.metrics_logger.flush() {
            warn!("Failed to flush resume metrics {:?}", e);
        }
        if self.options.dry_run {
            info!("Not launching resume image: in a dry run.");
            // Keep logs in memory, like launch_resume_image() does.
            // This also closes out the resume log.
            redirect_log(HiberlogOut::BufferInMemory);
            Ok(())
        } else {
            self.launch_resume_image(meta_file, frozen_userspace, pending_resume_call)
        }
    }

    /// Emits the signal upstart is waiting for to allow trunksd to start and
    /// consume all the TPM handles.
    fn emit_tpm_done_event(&mut self) -> Result<()> {
        if self.tpm_done_event_emitted {
            return Ok(());
        }

        // Trunksd is blocked from starting so that the kernel can use TPM
        // handles during early resume. Allow trunksd to start now that resume
        // TPM usage is complete. Note that trunksd will also start if hiberman
        // exits, so it's only critical that this is run if hiberman is going to
        // continue running for an indeterminate amount of time.
        emit_upstart_event("hibernate-tpm-done")
            .context("Failed to emit hibernate-tpm-done event")?;

        self.tpm_done_event_emitted = true;
        Ok(())
    }

    /// Load the resume image from disk into memory.
    fn read_image<'a>(
        &mut self,
        mut header_file: DiskFile,
        mut hiber_file: DiskFile,
        snap_dev: &mut SnapshotDevice,
        dbus_connection: &'a mut HiberDbusConnection,
    ) -> Result<Option<PendingResumeCall>> {
        let page_size = get_page_size();
        let mut image_size = self.metadata.image_size;
        let total_size = image_size;
        debug!("Resume image is {} bytes", image_size);
        // Create the image joiner, which combines the header file and the data
        // file into one contiguous read stream.
        let should_hash_header = (self.metadata.flags & META_FLAG_KERNEL_ENCRYPTED) == 0;
        let mut joiner = ImageJoiner::new(
            &mut header_file,
            &mut hiber_file,
            self.metadata.image_meta_size,
            should_hash_header,
        );
        self.load_header(&mut joiner, snap_dev)?;
        image_size -= self.metadata.image_meta_size;
        let mover_source: &mut dyn Read;

        // Fire up the preloader to start loading pages off of disk right away.
        // We preload data because disk latency tends to be the long pole in the
        // tent, but we don't yet have the decryption keys needed to decrypt the
        // data and feed it to the kernel. Preloading is an attempt to frontload
        // that disk latency while the user is still authenticating (eg typing
        // in their password).
        let mut preloader;
        let preload_duration;
        if self.options.no_preloader {
            info!("Not using preloader");
            mover_source = &mut joiner;
            preload_duration = Duration::ZERO;
        } else {
            preloader = ImagePreloader::new(
                &mut joiner,
                image_size
                    .try_into()
                    .expect("The whole image should fit in memory"),
            );

            // By now the kernel has done its own allocation for hibernate image
            // space. We can use the remaining memory to preload from disk.
            debug!("Preloading hibernate image");
            let start = Instant::now();
            let preloaded = preloader.load_into_available_memory()?;
            preload_duration = start.elapsed();
            log_io_duration("Preloaded image", preloaded as i64, preload_duration);
            self.metrics_logger.metrics_send_io_sample(
                "PreloadImage",
                preloaded as i64,
                preload_duration,
            );
            mover_source = &mut preloader;
        }

        // Now that as much data as possible has been preloaded from disk, the next
        // step is to start decrypting it and push it to the kernel. Block waiting
        // on the authentication key material from cryptohome.
        let pending_resume_call;
        if self.options.test_keys {
            self.key_manager.use_test_keys()?;
            pending_resume_call = None;
        } else {
            // This is what blocks waiting for seed material.
            let wait_start = Instant::now();
            pending_resume_call = Some(self.populate_seed(dbus_connection, true)?);
            let wait_duration = wait_start.elapsed();
            log_duration("Got seed", wait_duration);
            self.metrics_logger.metrics_send_duration_sample(
                "GotSeed",
                wait_duration,
                SEED_WAIT_METRIC_CEILING,
            );
        }

        // With the seed material in hand, decrypt the private data, and fire up
        // the big image decryptor.
        info!("Loading private metadata");
        let metadata = &mut self.metadata;
        self.key_manager.install_saved_metadata_key(metadata)?;
        metadata.load_private_data()?;

        // Let the kernel fold in the user key now that it's finally available.
        if (metadata.flags & META_FLAG_KERNEL_ENCRYPTED) != 0 {
            let user_key = UswsuspUserKey::new_from_u8_slice(&metadata.data_key);
            snap_dev
                .set_user_key(&user_key)
                .context("Failed to set user key")?;
        }

        let mode = if (metadata.flags & META_FLAG_ENCRYPTED) != 0 {
            CryptoMode::Decrypt
        } else {
            CryptoMode::Unencrypted
        };
        let mut decryptor = CryptoReader::new(
            mover_source,
            &metadata.data_key,
            &metadata.data_iv,
            mode,
            page_size * BUFFER_PAGES,
        )?;
        if (metadata.flags & META_FLAG_ENCRYPTED) != 0 {
            debug!("Image is encrypted");
        } else if (metadata.flags & META_FLAG_KERNEL_ENCRYPTED) != 0 {
            debug!("Kernel is handling encryption");
        } else if !self.options.unencrypted {
            error!("Unencrypted images are not permitted without --unencrypted");
            return Err(HibernateError::ImageUnencryptedError())
                .context("Detected unencrypted image without --unencrypted");
        }

        // The first data byte was already sent down. Send down a partial page
        // Move from the image, which can read big chunks, to the snapshot dev,
        // which only writes pages.
        debug!("Sending in partial page");
        self.read_first_partial_page(&mut decryptor, page_size, snap_dev)?;
        image_size -= page_size as i64;
        let main_move_start = Instant::now();
        // Fire up the big image pump into the kernel.
        let mut mover = ImageMover::new(
            &mut decryptor,
            &mut snap_dev.file,
            image_size as i64,
            page_size * BUFFER_PAGES,
            page_size,
        )?;

        mover.pad_input_length();
        mover
            .move_all()
            .context("Failed to move in hibernate image")?;
        let main_move_duration = main_move_start.elapsed();
        log_io_duration("Read main image", image_size, main_move_duration);
        self.metrics_logger
            .metrics_send_io_sample("ReadMainImage", image_size, main_move_duration);
        let total_io_duration = preload_duration + main_move_duration;
        log_io_duration("Read all data", total_size, total_io_duration);
        self.metrics_logger
            .metrics_send_io_sample("ReadAllData", total_size, total_io_duration);

        // Validate the image data tag.
        decryptor
            .check_tag(&self.metadata.data_tag)
            .context("Failed to verify image authentication tag")?;

        if should_hash_header {
            // Check the header pages hash. Ideally this would be done just
            // after the private data was loaded, but by then we've handed a
            // mutable borrow out to the mover source. This is fine too, as the
            // kernel will reject writes if the page list size is different. The
            // worst an attacker can do is move pages around to other RAM
            // locations (the kernel ensures the pages are RAM). The check here
            // ensures we'll never jump into anything but the original header.
            debug!("Validating header content");
            let mut header_hash = [0u8; META_HASH_SIZE];
            joiner.get_header_hash(&mut header_hash)?;
            let metadata = &mut self.metadata;
            if metadata.header_hash != header_hash {
                error!("Metadata header hash mismatch");
                return Err(HibernateError::HeaderContentHashMismatch())
                    .context("Failed to load verify resume header pages");
            }
        }

        Ok(pending_resume_call)
    }

    /// Load the page map directly into the kernel.
    fn load_header(
        &mut self,
        joiner: &mut ImageJoiner,
        snap_dev: &mut SnapshotDevice,
    ) -> Result<()> {
        // Pump the header pages directly into the kernel. After the header
        // pages are fed to the kernel, the kernel will then do a giant
        // allocation to make space for the resume image.
        let header_size = self.metadata.image_meta_size;
        debug!("Loading {} header bytes immediately", header_size);
        let start = Instant::now();
        let page_size = get_page_size();
        ImageMover::new(
            joiner,
            &mut snap_dev.file,
            header_size,
            page_size * BUFFER_PAGES,
            page_size,
        )?
        .move_all()
        .context("Failed to load in header file")?;
        debug!("Done loading header pages");

        // Also write the first data byte, which is what actually triggers
        // the kernel to do its big allocation.
        snap_dev
            .file
            .write_all(std::slice::from_ref(&self.metadata.first_data_byte))
            .context("Failed to write first byte to snapshot")?;

        log_io_duration("Loaded page map", header_size, start.elapsed());
        Ok(())
    }

    /// Block waiting for the hibernate key seed, and then feed it to the key
    /// manager.
    fn populate_seed(
        &mut self,
        dbus_connection: &mut HiberDbusConnection,
        resume_in_progress: bool,
    ) -> Result<PendingResumeCall> {
        // Block waiting for seed material from cryptohome.
        let pending_resume_call = dbus_connection.get_seed_material(resume_in_progress)?;
        debug!("Got seed material");
        self.key_manager
            .set_private_key(&pending_resume_call.secret_seed)?;
        Ok(pending_resume_call)
    }

    /// To get the kernel to do its big allocation, we sent one byte of data to
    /// it after sending the header pages. But now we're out of alignment for the
    /// main move. This function sends the rest of the page to get things
    /// realigned, and verifies the contents of the first byte. This keeps the
    /// ImageMover uncomplicated and none the wiser.
    fn read_first_partial_page(
        &mut self,
        source: &mut dyn Read,
        page_size: usize,
        snap_dev: &mut SnapshotDevice,
    ) -> Result<()> {
        let mut buffer = MmapBuffer::new(page_size)?;
        let buf = buffer.u8_slice_mut();
        // Get the whole page from the source, including the first byte.
        let bytes_read = source
            .read(buf)
            .context("Failed to read partial first page")?;
        if bytes_read != page_size {
            return Err(HibernateError::IoSizeError(format!(
                "Read only {} of {} byte",
                bytes_read, page_size
            )))
            .context("Failed to read first partial page");
        }

        if buf[0] != self.metadata.first_data_byte {
            // Print an error, but don't print the right answer.
            error!(
                "First data byte of {:x} was incorrect, expected {:x}",
                buf[0], self.metadata.first_data_byte
            );
            return Err(HibernateError::FirstDataByteMismatch())
                .context("Failed to read first partial page");
        }

        // Now write most of the page.
        let bytes_written = snap_dev
            .file
            .write(&buf[1..])
            .context("Failed to write first partial page")?;

        if bytes_written != page_size - 1 {
            return Err(HibernateError::IoSizeError(format!(
                "Wrote only {} of {} byte",
                bytes_written,
                page_size - 1
            )))
            .context("Failed to write first partial page");
        }

        Ok(())
    }

    /// Jump into the already-loaded resume image. The PendingResumeCall isn't
    /// actually used, but is received to enforce the lifetime of the object.
    /// This prevents bugs where it accidentally gets dropped by the caller too
    /// soon, allowing normal boot to proceed while resume is also in progress.
    fn launch_resume_image(
        &mut self,
        mut meta_file: BouncedDiskFile,
        mut frozen_userspace: FrozenUserspaceTicket,
        _pending_resume: Option<PendingResumeCall>,
    ) -> Result<()> {
        // Clear the valid flag and set the resume flag to indicate this image
        // was resumed into.
        let metadata = &mut self.metadata;
        metadata.flags &= !META_FLAG_VALID;
        metadata.flags |= META_FLAG_RESUME_LAUNCHED;
        meta_file.rewind()?;
        metadata.write_to_disk(&mut meta_file)?;
        meta_file.sync_all().context("Failed to sync metafile")?;

        // Jump into the restore image. This resumes execution in the lower
        // portion of suspend_system() on success. Flush and stop the logging
        // before control is lost.
        info!("Launching resume image");

        // Keep logs in memory for now, which also closes out the resume log file.
        redirect_log(HiberlogOut::BufferInMemory);
        let snap_dev = frozen_userspace.as_mut();
        let result = snap_dev.atomic_restore();
        error!("Resume failed");
        // If we are still executing then the resume failed. Mark it as such.
        metadata.flags |= META_FLAG_RESUME_FAILED;
        meta_file.rewind()?;
        metadata.write_to_disk(&mut meta_file)?;
        result
    }

    /// Save the public key for future hibernate attempts.
    fn save_public_key(&mut self, dbus_connection: &mut HiberDbusConnection) -> Result<()> {
        info!("Fetching public key for future hibernate");
        // Wait until the key material is available if the key is not already
        // populated from a failed resume attempt.
        if !self.key_manager.has_public_key() {
            self.populate_seed(dbus_connection, false)?;
        }

        self.key_manager.save_public_key()
    }
}
