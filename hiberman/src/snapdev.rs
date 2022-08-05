// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement snapshot device functionality.

use std::convert::TryInto;
use std::fs::{metadata, File, OpenOptions};
use std::os::unix::fs::FileTypeExt;
use std::path::Path;

use anyhow::{Context, Result};
use libc::{self, c_int, c_ulong, c_void, loff_t};
use log::{error, info};
use sys_util::{ioctl_io_nr, ioctl_ior_nr, ioctl_iow_nr, ioctl_iowr_nr};

use crate::hiberutil::HibernateError;

const SNAPSHOT_PATH: &str = "/dev/snapshot";

#[repr(C)]
#[repr(packed)]
#[derive(Copy, Clone)]
pub struct UswsuspKeyBlob {
    blob_len: u32,
    blob: [u8; 512],
    nonce: [u8; 16],
}

impl UswsuspKeyBlob {
    pub fn deserialize(bytes: &[u8]) -> Self {
        // This is safe because the structure is repr(C), packed, and made only
        // of primitives.
        unsafe {
            let result: UswsuspKeyBlob = std::ptr::read_unaligned(
                bytes[0..::std::mem::size_of::<UswsuspKeyBlob>()].as_ptr() as *const _,
            );
            result
        }
    }

    pub fn serialize(&self) -> &[u8] {
        // This is safe because the structure is repr(C), packed, and made only
        // of primitives.
        unsafe {
            ::std::slice::from_raw_parts(
                (self as *const UswsuspKeyBlob) as *const u8,
                ::std::mem::size_of::<UswsuspKeyBlob>(),
            )
        }
    }
}

// Ideally we'd be able to just derive Default for vectors with >32 elements,
// but alas, here we are.
impl Default for UswsuspKeyBlob {
    fn default() -> Self {
        Self {
            blob_len: Default::default(),
            blob: [0u8; 512],
            nonce: Default::default(),
        }
    }
}

#[repr(C)]
#[repr(packed)]
#[derive(Copy, Clone, Default)]
pub struct UswsuspUserKey {
    meta_size: i64,
    key_len: u32,
    key: [u8; 32],
    pad: u32,
}

impl UswsuspUserKey {
    pub fn new_from_u8_slice(key: &[u8]) -> Self {
        let key_len = key.len();
        let mut user_key = UswsuspUserKey::default();
        user_key.key[..key_len].copy_from_slice(key);
        user_key.key_len = key_len.try_into().expect("Silly key size");
        user_key
    }
}

// Define snapshot device ioctl numbers.
const SNAPSHOT_IOC_MAGIC: u32 = '3' as u32;

ioctl_io_nr!(SNAPSHOT_FREEZE, SNAPSHOT_IOC_MAGIC, 1);
ioctl_io_nr!(SNAPSHOT_UNFREEZE, SNAPSHOT_IOC_MAGIC, 2);
ioctl_io_nr!(SNAPSHOT_ATOMIC_RESTORE, SNAPSHOT_IOC_MAGIC, 4);
ioctl_ior_nr!(SNAPSHOT_GET_IMAGE_SIZE, SNAPSHOT_IOC_MAGIC, 14, u64);
ioctl_io_nr!(SNAPSHOT_PLATFORM_SUPPORT, SNAPSHOT_IOC_MAGIC, 15);
ioctl_io_nr!(SNAPSHOT_POWER_OFF, SNAPSHOT_IOC_MAGIC, 16);
ioctl_iow_nr!(SNAPSHOT_CREATE_IMAGE, SNAPSHOT_IOC_MAGIC, 17, u32);
ioctl_iowr_nr!(
    SNAPSHOT_ENABLE_ENCRYPTION,
    SNAPSHOT_IOC_MAGIC,
    21,
    UswsuspKeyBlob
);
ioctl_iowr_nr!(
    SNAPSHOT_SET_USER_KEY,
    SNAPSHOT_IOC_MAGIC,
    22,
    UswsuspUserKey
);

const FREEZE: u64 = SNAPSHOT_FREEZE();
const UNFREEZE: u64 = SNAPSHOT_UNFREEZE();
const ATOMIC_RESTORE: u64 = SNAPSHOT_ATOMIC_RESTORE();
const GET_IMAGE_SIZE: u64 = SNAPSHOT_GET_IMAGE_SIZE();
const PLATFORM_SUPPORT: u64 = SNAPSHOT_PLATFORM_SUPPORT();
const POWER_OFF: u64 = SNAPSHOT_POWER_OFF();
const CREATE_IMAGE: u64 = SNAPSHOT_CREATE_IMAGE();
const ENABLE_ENCRYPTION: u64 = SNAPSHOT_ENABLE_ENCRYPTION();
const SET_USER_KEY: u64 = SNAPSHOT_SET_USER_KEY();

/// The SnapshotDevice is mostly a group of method functions that send ioctls to
/// an open snapshot device file descriptor.
pub struct SnapshotDevice {
    pub file: File,
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

        let mut file = OpenOptions::new();
        let file = match mode {
            SnapshotMode::Read => file.read(true).write(false),
            SnapshotMode::Write => file.read(false).write(true),
        };

        let file = file
            .open(SNAPSHOT_PATH)
            .context("Failed to open snapshot device")?;

        Ok(SnapshotDevice { file })
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

    /// Jump into the fully loaded resume image. On success, this does not
    /// return, as it launches into the resumed image.
    pub fn atomic_restore(&mut self) -> Result<()> {
        // This is safe because either the entire world will stop executing,
        // or nothing happens, preserving Rust's guarantees.
        unsafe { self.simple_ioctl(ATOMIC_RESTORE, "ATOMIC_RESTORE") }
    }

    /// Return the size of the recently snapshotted hibernate image in bytes.
    pub fn get_image_size(&mut self) -> Result<loff_t> {
        let mut image_size: loff_t = 0;
        // This is safe because the ioctl modifies an loff_t sized integer,
        // we are passing down.
        unsafe {
            self.ioctl_with_mut_ptr(
                GET_IMAGE_SIZE,
                "GET_IMAGE_SIZE",
                &mut image_size as *mut loff_t as *mut c_void,
            )?;
        }
        Ok(image_size)
    }

    /// Indicate to the kernel whether or not to power down into "platform" mode
    /// (which is only meaningful on Intel systems, and means S4).
    pub fn set_platform_mode(&mut self, use_platform_mode: bool) -> Result<()> {
        let param_ulong: c_ulong = use_platform_mode as c_ulong;
        unsafe {
            let rc = sys_util::ioctl_with_val(&self.file, PLATFORM_SUPPORT, param_ulong);
            self.evaluate_ioctl_return("PLATFORM_SUPPORT", rc)
        }
    }

    /// Power down the system.
    pub fn power_off(&mut self) -> Result<()> {
        // This is safe because powering the system off does not violate any
        // Rust guarantees.
        unsafe { self.simple_ioctl(POWER_OFF, "POWER_OFF") }
    }

    /// Get the encryption key from the kernel.
    pub fn get_key_blob(&mut self) -> Result<UswsuspKeyBlob> {
        let mut blob = UswsuspKeyBlob::default();
        // The parameter may be either read or written to based on whether the
        // snap device was opened with read or write. The API assumes both.
        unsafe {
            self.ioctl_with_mut_ptr(
                ENABLE_ENCRYPTION,
                "ENABLE_ENCRYPTION",
                &mut blob as *mut UswsuspKeyBlob as *mut c_void,
            )?;
        }
        Ok(blob)
    }

    /// Hand the encryption key to the kernel. This is actually the same ioctl
    /// as the get call, but only one is successful depending on if the snapdev
    /// was opened with read or write permission.
    pub fn set_key_blob(&mut self, blob: &UswsuspKeyBlob) -> Result<()> {
        unsafe {
            self.ioctl_with_ptr(
                ENABLE_ENCRYPTION,
                "ENABLE_ENCRYPTION",
                blob as *const UswsuspKeyBlob as *const c_void,
            )
        }
    }

    /// Set the user key for hibernate data. Returns the metadata size
    /// from the kernel on success.
    pub fn set_user_key(&mut self, key: &UswsuspUserKey) -> Result<i64> {
        let mut key_copy = *key;
        unsafe {
            self.ioctl_with_mut_ptr(
                SET_USER_KEY,
                "SET_USER_KEY",
                &mut key_copy as *mut UswsuspUserKey as *mut c_void,
            )?;
        }

        Ok(key_copy.meta_size)
    }

    /// Helper function to send an ioctl with no parameter and return a result.
    /// # Safety
    ///
    /// The caller must ensure that the actions the ioctl performs uphold
    /// Rust's memory safety guarantees. In this case, no parameter is being
    /// passed, so those guarantees would mostly be concerned with larger
    /// address space layout or memory model side effects.
    unsafe fn simple_ioctl(&mut self, ioctl: c_ulong, name: &str) -> Result<()> {
        let rc = sys_util::ioctl(&self.file, ioctl);
        self.evaluate_ioctl_return(name, rc)
    }

    /// Helper function to send an ioctl and return a Result
    /// # Safety
    ///
    /// The caller must ensure that the actions the ioctl performs uphold
    /// Rust's memory safety guarantees. Specifically
    unsafe fn ioctl_with_ptr(
        &mut self,
        ioctl: c_ulong,
        name: &str,
        param: *const c_void,
    ) -> Result<()> {
        let rc = sys_util::ioctl_with_ptr(&self.file, ioctl, param);
        self.evaluate_ioctl_return(name, rc)
    }

    /// Helper function to send an ioctl and return a Result
    /// # Safety
    ///
    /// The caller must ensure that the actions the ioctl performs uphold
    /// Rust's memory safety guarantees. Specifically
    unsafe fn ioctl_with_mut_ptr(
        &mut self,
        ioctl: c_ulong,
        name: &str,
        param: *mut c_void,
    ) -> Result<()> {
        let rc = sys_util::ioctl_with_mut_ptr(&self.file, ioctl, param);
        self.evaluate_ioctl_return(name, rc)
    }

    fn evaluate_ioctl_return(&mut self, name: &str, rc: c_int) -> Result<()> {
        if rc < 0 {
            return Err(HibernateError::SnapshotIoctlError(
                name.to_string(),
                sys_util::Error::last(),
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
