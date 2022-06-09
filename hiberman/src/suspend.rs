// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement hibernate suspend functionality

use std::ffi::CString;
use std::io::Write;
use std::mem::MaybeUninit;
use std::time::Instant;

use anyhow::{Context, Result};
use libc::{loff_t, reboot, RB_POWER_OFF};
use log::{debug, error, info, warn};
use sys_util::syscall;

use crate::cookie::{set_hibernate_cookie, HibernateCookieValue};
use crate::crypto::{CryptoMode, CryptoWriter};
use crate::diskfile::{BouncedDiskFile, DiskFile};
use crate::files::{
    does_hiberfile_exist, open_metrics_file, preallocate_header_file, preallocate_hiberfile,
    preallocate_kernel_key_file, preallocate_log_file, preallocate_metadata_file,
    preallocate_metrics_file, STATEFUL_DIR,
};
use crate::hiberlog::{flush_log, redirect_log, replay_logs, reset_log, HiberlogFile, HiberlogOut};
use crate::hibermeta::{
    HibernateMetadata, META_FLAG_ENCRYPTED, META_FLAG_KERNEL_ENCRYPTED, META_FLAG_VALID,
    META_TAG_SIZE,
};
use crate::hiberutil::HibernateOptions;
use crate::hiberutil::{
    get_page_size, lock_process_memory, log_duration, log_io_duration, path_to_stateful_block,
    prealloc_mem, HibernateError, BUFFER_PAGES,
};
use crate::imagemover::ImageMover;
use crate::keyman::HibernateKeyManager;
use crate::lvm::is_lvm_system;
use crate::metrics::{log_hibernate_attempt, read_and_send_metrics, MetricsFile, MetricsLogger};
use crate::snapdev::{FrozenUserspaceTicket, SnapshotDevice, SnapshotMode, UswsuspUserKey};
use crate::splitter::ImageSplitter;
use crate::sysfs::Swappiness;
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
    metadata: HibernateMetadata,
    metrics: MetricsLogger,
    volume_manager: VolumeManager,
}

impl SuspendConductor {
    /// Create a new SuspendConductor in preparation for imminent hibernation.
    pub fn new() -> Result<Self> {
        Ok(SuspendConductor {
            options: Default::default(),
            metadata: HibernateMetadata::new()?,
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
        if let Err(e) = log_hibernate_attempt() {
            warn!("Failed to log hibernate attempt: \n {}", e);
        }

        self.volume_manager.setup_hibernate_lv(true)?;
        self.volume_manager.create_lv_snapshot_files()?;
        let is_lvm = is_lvm_system()?;
        let files_exist = does_hiberfile_exist();
        let should_zero = is_lvm && !files_exist;
        let header_file = preallocate_header_file(should_zero)?;
        let hiber_file = preallocate_hiberfile(should_zero)?;
        let kernel_key_file = preallocate_kernel_key_file(should_zero)?;
        let meta_file = preallocate_metadata_file(should_zero)?;

        // The resume log file needs to be preallocated now before the
        // snapshot is taken, though it's not used here.
        preallocate_log_file(HiberlogFile::Resume, should_zero)?;
        preallocate_metrics_file(MetricsFile::Resume, should_zero)?;
        preallocate_metrics_file(MetricsFile::Suspend, should_zero)?;
        let metrics_file = open_metrics_file(MetricsFile::Suspend)?;
        self.metrics.file = Some(metrics_file);
        let mut log_file = preallocate_log_file(HiberlogFile::Suspend, should_zero)?;
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
        // Don't allow the logfile to log as it creates a deadlock.
        log_file.set_logging(false);
        let fs_stats = Self::get_fs_stats()?;
        let _locked_memory = lock_process_memory()?;
        let mut swappiness = Swappiness::new()?;
        swappiness.set_swappiness(SUSPEND_SWAPPINESS)?;
        let mut key_manager = HibernateKeyManager::new();
        // Set up the hibernate metadata encryption keys. This was populated
        // at login time by a previous instance of this process.
        if self.options.test_keys {
            key_manager.use_test_keys()?;
        } else {
            key_manager.load_public_key()?;
        }

        // Now that the public key is loaded, derive a metadata encryption key.
        key_manager.install_new_metadata_key(&mut self.metadata)?;

        // Stop logging to syslog, and divert instead to a file since the
        // logging daemon's about to be frozen.
        redirect_log(HiberlogOut::File(Box::new(log_file)));
        debug!("Syncing filesystems");
        // This is safe because sync() does not modify memory.
        unsafe {
            libc::sync();
        }

        prealloc_mem(&mut self.metrics).context("Failed to preallocate memory for hibernate")?;

        let result = self.suspend_system(header_file, hiber_file, meta_file, kernel_key_file);
        // Now send any remaining logs and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);
        // Replay logs first because they happened earlier.
        replay_logs(
            result.is_ok() && !self.options.dry_run,
            !self.options.dry_run,
        );
        // Read the metrics files and send out the samples.
        read_and_send_metrics();
        self.delete_data_if_disk_full(fs_stats);
        result
    }

    /// Inner helper function to actually take the snapshot, save it to disk,
    /// and shut down. Returns upon a failure to hibernate, or after a
    /// successful hibernation has resumed.
    fn suspend_system(
        &mut self,
        header_file: DiskFile,
        hiber_file: DiskFile,
        meta_file: BouncedDiskFile,
        kernel_key_file: BouncedDiskFile,
    ) -> Result<()> {
        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Read)?;
        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;
        self.snapshot_and_save(
            header_file,
            hiber_file,
            meta_file,
            kernel_key_file,
            frozen_userspace,
        )
    }

    /// Attempt to retrieve and save the kernel key blob on systems that support
    /// it. Failure to get the key blob indicates a system that does not support
    /// it, and is not treated as an error. Returns a boolean indicating whether
    /// or not the kernel supports encrypting the hibernate image.
    fn save_kernel_key_blob(
        &mut self,
        snap_dev: &mut SnapshotDevice,
        key_file: &mut BouncedDiskFile,
    ) -> Result<bool> {
        let kernel_key_blob = match snap_dev.get_key_blob() {
            Err(e) => {
                info!("No in-kernel encryption support ({:?}), continuing", e);
                return Ok(false);
            }
            Ok(k) => k,
        };
        let blob_bytes = kernel_key_blob.serialize();
        let mut blob_buf = [0u8; 4096];
        blob_buf[0..blob_bytes.len()].copy_from_slice(blob_bytes);
        key_file.write_all(&blob_buf).context(format!(
            "Could not write kernel key blob len {}",
            blob_buf.len()
        ))?;
        // Mark that the kernel is doing encryption.
        self.metadata.flags |= META_FLAG_KERNEL_ENCRYPTED;
        Ok(true)
    }

    /// Snapshot the system, write the result to disk, and power down. Returns
    /// upon failure to hibernate, or after a hibernated system has successfully
    /// resumed.
    fn snapshot_and_save(
        &mut self,
        header_file: DiskFile,
        hiber_file: DiskFile,
        mut meta_file: BouncedDiskFile,
        mut kernel_key_file: BouncedDiskFile,
        mut frozen_userspace: FrozenUserspaceTicket,
    ) -> Result<()> {
        let block_path = path_to_stateful_block()?;
        let dry_run = self.options.dry_run;
        let snap_dev = frozen_userspace.as_mut();
        // Attempt to get the key blob from the kernel. If this was
        // successful, the kernel is handling encryption, so we don't need
        // to ourselves.
        let kernel_encryption = if self.options.no_kernel_encryption {
            false
        } else {
            self.save_kernel_key_blob(snap_dev, &mut kernel_key_file)?
        };

        // Attempt to get the key blob from the kernel. If this was
        // successful, the kernel is handling encryption, so we don't need
        // to ourselves.
        if kernel_encryption {
            info!("Using kernel image encryption");
            self.options.unencrypted = true;
        }

        let platform_mode = self.options.force_platform_mode;
        snap_dev.set_platform_mode(platform_mode)?;
        // This is where the suspend path and resume path fork. On success,
        // both halves of these conditions execute, just at different times.
        if snap_dev.atomic_snapshot()? {
            // Suspend path. Everything after this point is invisible to the
            // hibernated kernel.

            // Send in the user key, and get out the image metadata size.
            if kernel_encryption {
                let user_key = UswsuspUserKey::new_from_u8_slice(&self.metadata.data_key);
                self.metadata.image_meta_size = snap_dev.set_user_key(&user_key)?;
            }
            self.write_image(header_file, hiber_file, snap_dev)?;
            meta_file.rewind()?;
            self.metadata.write_to_disk(&mut meta_file)?;
            drop(meta_file);
            // Set the hibernate cookie so the next boot knows to start in RO mode.
            info!("Setting hibernate cookie at {}", block_path);
            set_hibernate_cookie(Some(&block_path), HibernateCookieValue::ResumeReady)?;
            if dry_run {
                info!("Not powering off due to dry run");
            } else if platform_mode {
                info!("Entering S4");
            } else {
                info!("Powering off");
            }

            // Flush out the hibernate log, and instead keep logs in memory.
            // Any logs beyond here are lost upon powerdown.
            if let Err(e) = self.metrics.flush() {
                warn!("Failed to flush suspend metrics {:?}", e);
            }
            flush_log();
            redirect_log(HiberlogOut::BufferInMemory);
            // Power the thing down.
            if !dry_run {
                if platform_mode {
                    snap_dev.power_off()?;
                } else {
                    Self::power_off()?;
                }

                error!("Returned from power off");
            }

            // Unset the hibernate cookie.
            info!("Unsetting hibernate cookie at {}", block_path);
            set_hibernate_cookie(Some(&block_path), HibernateCookieValue::NoResume)?;
        } else {
            // This is the resume path. First, forcefully reset the logger, which is some
            // stale partial state that the suspend path ultimately flushed and closed.
            // Keep logs in memory for now.
            reset_log();
            redirect_log(HiberlogOut::BufferInMemory);
            info!("Resumed from hibernate");
        }

        Ok(())
    }

    /// Save the snapshot image to disk.
    fn write_image(
        &mut self,
        mut header_file: DiskFile,
        mut hiber_file: DiskFile,
        snap_dev: &mut SnapshotDevice,
    ) -> Result<()> {
        let image_size = snap_dev.get_image_size()?;
        let page_size = get_page_size();
        let mode = if self.options.unencrypted {
            CryptoMode::Unencrypted
        } else {
            CryptoMode::Encrypt
        };
        let mut encryptor = CryptoWriter::new(
            &mut hiber_file,
            &self.metadata.data_key,
            &self.metadata.data_iv,
            mode,
            page_size * BUFFER_PAGES,
        )?;

        if !self.options.unencrypted {
            self.metadata.flags |= META_FLAG_ENCRYPTED;
            debug!("Added encryption");
        }

        debug!("Hibernate image is {} bytes", image_size);
        let compute_header_hash = (self.metadata.flags & META_FLAG_KERNEL_ENCRYPTED) == 0;
        let start = Instant::now();
        let mut splitter = ImageSplitter::new(
            &mut header_file,
            &mut encryptor,
            &mut self.metadata,
            compute_header_hash,
        );
        Self::move_image(snap_dev, &mut splitter, image_size)?;
        let image_duration = start.elapsed();
        log_io_duration("Wrote hibernate image", image_size, image_duration);
        self.metrics
            .metrics_send_io_sample("WriteHibernateImage", image_size, image_duration);
        self.metadata.data_tag = encryptor.get_tag()?;

        assert!(self.metadata.data_tag != [0u8; META_TAG_SIZE]);

        self.metadata.image_size = image_size;
        self.metadata.flags |= META_FLAG_VALID;
        Ok(())
    }

    /// Move the image in stages, first the header, then the body. This is done
    /// because when using kernel encryption, the header size won't align to a
    /// page. But we still want the main data DiskFile to use DIRECT_IO with
    /// page-aligned buffers. By stopping after the header, we can ensure that
    /// the main data I/O pumps through in page-aligned chunks.
    fn move_image(
        snap_dev: &mut SnapshotDevice,
        splitter: &mut ImageSplitter,
        image_size: loff_t,
    ) -> Result<()> {
        let page_size = get_page_size();
        // If the header size is not known, move a single page so the splitter
        // can parse the header and figure it out.
        let meta_size = if splitter.meta_size == 0 {
            ImageMover::new(
                &mut snap_dev.file,
                splitter,
                page_size as i64,
                page_size,
                page_size,
            )?
            .move_all()
            .context("Failed to move first page")?;

            splitter.meta_size - (page_size as i64)
        } else {
            splitter.meta_size
        };

        assert!(splitter.meta_size != 0);

        // Move the header portion. Pad the header size out to a page.
        let mut mover = ImageMover::new(
            &mut snap_dev.file,
            splitter,
            meta_size,
            page_size,
            page_size * BUFFER_PAGES,
        )?;
        mover
            .move_all()
            .context("Failed to write out header pages")?;
        drop(mover);

        // Now move the main image data.
        let meta_size = splitter.meta_size;
        let mut mover = ImageMover::new(
            &mut snap_dev.file,
            splitter,
            image_size - meta_size,
            page_size,
            page_size * BUFFER_PAGES,
        )?;
        mover.pad_output_length();
        mover.move_all().context("Failed to write out data pages")
    }

    /// Clean up the hibernate files, releasing that space back to other usermode apps.
    fn delete_data_if_disk_full(&mut self, fs_stats: libc::statvfs) {
        let free_percent = fs_stats.f_bfree * 100 / fs_stats.f_blocks;
        if free_percent < LOW_DISK_FREE_THRESHOLD_PERCENT {
            debug!("Freeing hiberdata: FS is only {}% free", free_percent);
            // TODO: Unlink hiberfile and metadata.
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
            Err(HibernateError::ShutdownError(sys_util::Error::last()))
                .context("Failed to shut down")
        }
    }
}
