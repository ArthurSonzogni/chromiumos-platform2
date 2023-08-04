// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements snapshot device functionality.

use std::fs::metadata;
use std::fs::File;
use std::fs::OpenOptions;
use std::os::unix::fs::FileTypeExt;
use std::path::Path;

use anyhow::Context;
use anyhow::Result;
use libc::c_int;
use libc::c_long;
use libc::c_ulong;
use libc::c_void;
use libc::{self};
use log::error;
use log::info;

use crate::hiberutil::get_device_id;
use crate::hiberutil::HibernateError;
use crate::ioctl::ioctl_io_nr;
use crate::ioctl::ioctl_ior_nr;
use crate::ioctl::ioctl_iow_nr;
use crate::ioctl::ioctl_with_mut_ptr;
use crate::ioctl::ioctl_with_ptr;
use crate::ioctl::ioctl_with_val;

const SNAPSHOT_PATH: &str = "/dev/snapshot";

// Define snapshot device ioctl numbers.
const SNAPSHOT_IOC_MAGIC: u32 = '3' as u32;

ioctl_io_nr!(SNAPSHOT_FREEZE, SNAPSHOT_IOC_MAGIC, 1);
ioctl_io_nr!(SNAPSHOT_UNFREEZE, SNAPSHOT_IOC_MAGIC, 2);
ioctl_io_nr!(SNAPSHOT_ATOMIC_RESTORE, SNAPSHOT_IOC_MAGIC, 4);
ioctl_ior_nr!(SNAPSHOT_GET_IMAGE_SIZE, SNAPSHOT_IOC_MAGIC, 14, u64);
ioctl_io_nr!(SNAPSHOT_PLATFORM_SUPPORT, SNAPSHOT_IOC_MAGIC, 15);
ioctl_io_nr!(SNAPSHOT_POWER_OFF, SNAPSHOT_IOC_MAGIC, 16);
ioctl_iow_nr!(SNAPSHOT_CREATE_IMAGE, SNAPSHOT_IOC_MAGIC, 17, u32);
ioctl_iow_nr!(SNAPSHOT_SET_BLOCK_DEVICE, SNAPSHOT_IOC_MAGIC, 40, u32);
ioctl_io_nr!(SNAPSHOT_XFER_BLOCK_DEVICE, SNAPSHOT_IOC_MAGIC, 41);
ioctl_io_nr!(SNAPSHOT_RELEASE_BLOCK_DEVICE, SNAPSHOT_IOC_MAGIC, 42);

const FREEZE: u64 = SNAPSHOT_FREEZE();
const UNFREEZE: u64 = SNAPSHOT_UNFREEZE();
const ATOMIC_RESTORE: u64 = SNAPSHOT_ATOMIC_RESTORE();
const GET_IMAGE_SIZE: u64 = SNAPSHOT_GET_IMAGE_SIZE();
const PLATFORM_SUPPORT: u64 = SNAPSHOT_PLATFORM_SUPPORT();
const POWER_OFF: u64 = SNAPSHOT_POWER_OFF();
const CREATE_IMAGE: u64 = SNAPSHOT_CREATE_IMAGE();
const SET_BLOCK_DEVICE: u64 = SNAPSHOT_SET_BLOCK_DEVICE();
const XFER_BLOCK_DEVICE: u64 = SNAPSHOT_XFER_BLOCK_DEVICE();
const RELEASE_BLOCK_DEVICE: u64 = SNAPSHOT_RELEASE_BLOCK_DEVICE();

/// The SnapshotDevice is mostly a group of method functions that send ioctls to
/// an open snapshot device file descriptor.
pub struct SnapshotDevice {
    file: File,
}

/// Define the possible modes in which to open the snapshot device.
pub enum SnapshotMode {
    Read,
    Write,
}

impl SnapshotDevice {
    /// Open the snapshot device and return a new object.
    pub fn new(mode: SnapshotMode) -> Result<SnapshotDevice> {
        if !Path::new(SNAPSHOT_PATH).exists() {
            return Err(HibernateError::SnapshotError(format!(
                "Snapshot device {} does not exist",
                SNAPSHOT_PATH
            )))
            .context("Failed to open snapshot device");
        }

        let snapshot_meta =
            metadata(SNAPSHOT_PATH).context("Failed to get file metadata for snapshot device")?;
        if !snapshot_meta.file_type().is_char_device() {
            return Err(HibernateError::SnapshotError(format!(
                "Snapshot device {} is not a character device",
                SNAPSHOT_PATH
            )))
            .context("Failed to open snapshot device");
        }

        let mut options = OpenOptions::new();
        let options = match mode {
            SnapshotMode::Read => options.read(true).write(false),
            SnapshotMode::Write => options.read(false).write(true),
        };

        let file = options
            .open(SNAPSHOT_PATH)
            .context("Failed to open snapshot device")?;

        Ok(SnapshotDevice { file })
    }

    /// Load a snapshot image from a file into the kernel.
    pub fn load_image(&mut self) -> Result<u64> {
        if let Err(e) = self.transfer_block_device() {
            error!("Failed to load image: {:?}", e);
            return Err(e);
        }
        self.get_image_size()
    }

    /// Freeze userspace, stopping all userspace processes except this one.
    pub fn freeze_userspace(&mut self) -> Result<FrozenUserspaceTicket> {
        // This is safe because the ioctl doesn't modify memory in a way that
        // violates Rust's guarantees.
        unsafe {
            self.simple_ioctl(FREEZE, "FREEZE")?;
        }
        Ok(FrozenUserspaceTicket { snap_dev: self })
    }

    /// Unfreeze userspace, resuming all other previously frozen userspace
    /// processes.
    pub fn unfreeze_userspace(&mut self) -> Result<()> {
        // This is safe because the ioctl doesn't modify memory in a way that
        // violates Rust's guarantees.
        unsafe { self.simple_ioctl(UNFREEZE, "UNFREEZE") }
    }

    /// Ask the kernel to create its hibernate snapshot. Returns a boolean
    /// indicating whether this process is executing in suspend (true) or resume
    /// (false). Like setjmp(), this function effectively returns twice: once
    /// after the snapshot image is created (true), and again when the
    /// hibernated image is restored (false).
    pub fn atomic_snapshot(&mut self) -> Result<bool> {
        let mut in_suspend: c_int = 0;
        // This is safe because the ioctl modifies a u32 sized integer, which
        // we have preinitialized and passed in.
        unsafe {
            self.ioctl_with_mut_ptr(
                CREATE_IMAGE,
                "CREATE_IMAGE",
                &mut in_suspend as *mut c_int as *mut c_void,
            )?;
        }
        Ok(in_suspend != 0)
    }

    pub fn set_block_device(&mut self, path: &Path) -> Result<()> {
        let dev_id = get_device_id(path)?;
        unsafe {
            let rc = ioctl_with_val(&self.file, SET_BLOCK_DEVICE, dev_id as c_ulong);
            self.evaluate_ioctl_return("SET_BLOCK_DEVICE", rc)
        }
    }

    pub fn release_block_device(&mut self) -> Result<()> {
        // This is safe because the ioctl doesn't modify memory in a way that
        // violates Rust's guarantees.
        unsafe { self.simple_ioctl(RELEASE_BLOCK_DEVICE, "RELEASE_BLOCK_DEVICE") }
    }

    pub fn transfer_block_device(&mut self) -> Result<()> {
        unsafe { self.simple_ioctl(XFER_BLOCK_DEVICE, "XFER_BLOCK_DEVICE") }
    }

    /// Jump into the fully loaded resume image. On success, this does not
    /// return, as it launches into the resumed image.
    pub fn atomic_restore(&mut self) -> Result<()> {
        // This is safe because either the entire world will stop executing,
        // or nothing happens, preserving Rust's guarantees.
        unsafe { self.simple_ioctl(ATOMIC_RESTORE, "ATOMIC_RESTORE") }
    }

    /// Get the size of the snapshot image.
    pub fn get_image_size(&mut self) -> Result<u64> {
        let mut image_size: c_long = 0;
        // This is safe because the ioctl modifies a u64 sized integer, which
        // we have preinitialized and passed in.
        unsafe {
            self.ioctl_with_mut_ptr(
                GET_IMAGE_SIZE,
                "GET_IMAGE_SIZE",
                &mut image_size as *mut c_long as *mut c_void,
            )?;
        }

        Ok(image_size.try_into().unwrap())
    }

    /// Power the system down using the platform driver (e.g. ACPI).
    pub fn power_off_platform_mode(&mut self) -> Result<()> {
        // This is safe because powering the system off does not violate any
        // Rust guarantees.
        unsafe {
            let rc = ioctl_with_val(&self.file, PLATFORM_SUPPORT, 1);
            self.evaluate_ioctl_return("PLATFORM_SUPPORT", rc)?;

            self.simple_ioctl(POWER_OFF, "POWER_OFF")
        }
    }

    /// Helper function to send an ioctl with no parameter and return a result.
    /// # Safety
    ///
    /// The caller must ensure that the actions the ioctl performs uphold
    /// Rust's memory safety guarantees. In this case, no parameter is being
    /// passed, so those guarantees would mostly be concerned with larger
    /// address space layout or memory model side effects.
    unsafe fn simple_ioctl(&mut self, ioctl: c_ulong, name: &str) -> Result<()> {
        let rc = crate::ioctl::ioctl(&self.file, ioctl);
        self.evaluate_ioctl_return(name, rc)
    }

    /// Helper function to send an ioctl and return a Result.
    /// # Safety
    ///
    /// The caller must ensure that the actions the ioctl performs uphold
    /// Rust's memory safety guarantees.
    #[allow(dead_code)]
    unsafe fn ioctl_with_ptr(
        &mut self,
        ioctl: c_ulong,
        name: &str,
        param: *const c_void,
    ) -> Result<()> {
        let rc = ioctl_with_ptr(&self.file, ioctl, param);
        self.evaluate_ioctl_return(name, rc)
    }

    /// Helper function to send an ioctl and return a Result
    /// # Safety
    ///
    /// The caller must ensure that the actions the ioctl performs uphold
    /// Rust's memory safety guarantees.
    unsafe fn ioctl_with_mut_ptr(
        &mut self,
        ioctl: c_ulong,
        name: &str,
        param: *mut c_void,
    ) -> Result<()> {
        let rc = ioctl_with_mut_ptr(&self.file, ioctl, param);
        self.evaluate_ioctl_return(name, rc)
    }

    fn evaluate_ioctl_return(&mut self, name: &str, rc: c_int) -> Result<()> {
        if rc < 0 {
            return Err(HibernateError::SnapshotIoctlError(
                name.to_string(),
                nix::Error::last(),
            ))
            .context("Failed to execute ioctl on snapshot device");
        }

        Ok(())
    }
}

/// A structure that wraps the SnapshotDevice, and unfreezes userspace when
/// dropped.
pub struct FrozenUserspaceTicket<'a> {
    snap_dev: &'a mut SnapshotDevice,
}

impl Drop for FrozenUserspaceTicket<'_> {
    fn drop(&mut self) {
        info!("Unfreezing userspace");
        if let Err(e) = self.snap_dev.unfreeze_userspace() {
            error!("Failed to unfreeze userspace: {}", e);
        }
    }
}

impl<'a> AsMut<SnapshotDevice> for FrozenUserspaceTicket<'a> {
    fn as_mut(&mut self) -> &mut SnapshotDevice {
        self.snap_dev
    }
}
