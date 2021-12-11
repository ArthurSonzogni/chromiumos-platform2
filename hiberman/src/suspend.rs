// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement hibernate suspend functionality

use std::ffi::CString;
use std::io::Write;
use std::mem::MaybeUninit;

use anyhow::{Context, Result};
use log::{debug, error, info, warn};
use sys_util::syscall;

use crate::cookie::set_hibernate_cookie;
use crate::crypto::{CryptoWriter, Mode};
use crate::diskfile::{BouncedDiskFile, DiskFile};
use crate::files::{
    create_hibernate_dir, preallocate_header_file, preallocate_hiberfile, preallocate_log_file,
    preallocate_metadata_file, HIBERNATE_DIR,
};
use crate::hiberlog::{flush_log, redirect_log, replay_logs, reset_log, HiberlogFile, HiberlogOut};
use crate::hibermeta::{HibernateMetadata, META_FLAG_ENCRYPTED, META_FLAG_VALID};
use crate::hiberutil::HibernateOptions;
use crate::hiberutil::{get_page_size, lock_process_memory, path_to_stateful_block, BUFFER_PAGES};
use crate::imagemover::ImageMover;
use crate::keyman::HibernateKeyManager;
use crate::snapdev::{FrozenUserspaceTicket, SnapshotDevice, SnapshotMode};
use crate::splitter::ImageSplitter;
use crate::sysfs::Swappiness;

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
}

impl SuspendConductor {
    /// Create a new SuspendConductor in preparation for imminent hibernation.
    pub fn new() -> Result<Self> {
        Ok(SuspendConductor {
            options: Default::default(),
            metadata: HibernateMetadata::new()?,
        })
    }

    /// Public entry point that hibernates the system, and returns either upon
    /// failure to hibernate or after the system has resumed from a successful
    /// hibernation.
    pub fn hibernate(&mut self, options: HibernateOptions) -> Result<()> {
        info!("Beginning hibernate");
        create_hibernate_dir()?;
        let header_file = preallocate_header_file()?;
        let hiber_file = preallocate_hiberfile()?;
        let meta_file = preallocate_metadata_file()?;
        self.options = options;

        // The resume log file needs to be preallocated now before the
        // snapshot is taken, though it's not used here.
        preallocate_log_file(HiberlogFile::Resume)?;
        let mut log_file = preallocate_log_file(HiberlogFile::Suspend)?;
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

        let result = self.suspend_system(header_file, hiber_file, meta_file);
        // Now send any remaining logs and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);
        // Replay logs first because they happened earlier.
        replay_logs(
            result.is_ok() && !self.options.dry_run,
            !self.options.dry_run,
        );
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
    ) -> Result<()> {
        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Read)?;
        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;
        self.snapshot_and_save(header_file, hiber_file, meta_file, frozen_userspace)
    }

    /// Snapshot the system, write the result to disk, and power down. Returns
    /// upon failure to hibernate, or after a hibernated system has successfully
    /// resumed.
    fn snapshot_and_save(
        &mut self,
        header_file: DiskFile,
        hiber_file: DiskFile,
        mut meta_file: BouncedDiskFile,
        mut frozen_userspace: FrozenUserspaceTicket,
    ) -> Result<()> {
        let block_path = path_to_stateful_block()?;
        let dry_run = self.options.dry_run;
        let snap_dev = frozen_userspace.as_mut();
        snap_dev.set_platform_mode(false)?;
        // This is where the suspend path and resume path fork. On success,
        // both halves of these conditions execute, just at different times.
        if snap_dev.atomic_snapshot()? {
            // Suspend path. Everything after this point is invisible to the
            // hibernated kernel.
            self.write_image(header_file, hiber_file, snap_dev)?;
            meta_file.rewind()?;
            self.metadata.write_to_disk(&mut meta_file)?;
            drop(meta_file);
            // Set the hibernate cookie so the next boot knows to start in RO mode.
            info!("Setting hibernate cookie at {}", block_path);
            set_hibernate_cookie(Some(&block_path), true)?;
            if dry_run {
                info!("Not powering off due to dry run");
            } else {
                info!("Powering off");
            }

            // Flush out the hibernate log, and instead keep logs in memory.
            // Any logs beyond here are lost upon powerdown.
            flush_log();
            redirect_log(HiberlogOut::BufferInMemory);
            // Power the thing down.
            if !dry_run {
                snap_dev.power_off()?;
                error!("Returned from power off");
            }

            // Unset the hibernate cookie.
            info!("Unsetting hibernate cookie at {}", block_path);
            set_hibernate_cookie(Some(&block_path), false)?;
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
        let mut mover_dest: &mut dyn Write = &mut hiber_file;
        let mut encryptor;
        if !self.options.unencrypted {
            encryptor = CryptoWriter::new(
                mover_dest,
                &self.metadata.data_key,
                &self.metadata.data_iv,
                Mode::Encrypt,
                page_size * BUFFER_PAGES,
            )?;
            mover_dest = &mut encryptor;
            self.metadata.flags |= META_FLAG_ENCRYPTED;
            debug!("Added encryption");
        } else {
            warn!("Warning: The hibernate image is unencrypted");
        }

        debug!("Hibernate image is {} bytes", image_size);
        let mut splitter = ImageSplitter::new(&mut header_file, mover_dest, &mut self.metadata);
        let mut writer = ImageMover::new(
            &mut snap_dev.file,
            &mut splitter,
            image_size,
            page_size,
            page_size * BUFFER_PAGES,
        )?;
        writer
            .move_all()
            .context("Failed to write out main image")?;
        info!("Wrote {} MB", image_size / 1024 / 1024);
        self.metadata.image_size = image_size as u64;
        self.metadata.flags |= META_FLAG_VALID;
        Ok(())
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
        let path = CString::new(HIBERNATE_DIR).unwrap();
        let mut stats: MaybeUninit<libc::statvfs> = MaybeUninit::zeroed();

        // This is safe because only stats is modified.
        syscall!(unsafe { libc::statvfs(path.as_ptr(), stats.as_mut_ptr()) })?;

        // Safe because the syscall just initialized it, and we just verified
        // the return was successful.
        unsafe { Ok(stats.assume_init()) }
    }
}
