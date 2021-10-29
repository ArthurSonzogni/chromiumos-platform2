// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement fiemap support, which can tell you the underlying disk extents
//! backing a file.

use std::fs::File;
use std::mem;
use std::os::unix::io::AsRawFd;

use anyhow::{Context, Result};
use libc::c_void;
use log::{debug, error};
use sys_util::ioctl_iowr_nr;

use crate::hiberutil::HibernateError;

ioctl_iowr_nr!(FS_IOC_FIEMAP, 'f' as u32, 11, C_Fiemap);
/// Define the Linux ioctl number for getting the fiemap.
const FIEMAP: u64 = FS_IOC_FIEMAP();

/// The C_Fiemap structure's format is mandated by the FS_IOC_FIEMAP ioctl. See
/// the linux man pages for details.
#[repr(C)]
struct C_Fiemap {
    fm_start: u64,
    fm_length: u64,
    fm_flags: u32,
    fm_mapped_extents: u32,
    fm_extent_count: u32,
    fm_reserved: u32,
}

/// The FiemapExtent structure's format is mandated by the FS_IOC_FIEMAP ioctl.
/// See the linux man pages for details.
#[repr(C)]
#[derive(Copy, Clone, Default)]
pub struct FiemapExtent {
    pub fe_logical: u64,
    pub fe_physical: u64,
    pub fe_length: u64,
    fe_reserved64: [u64; 2],
    pub fe_flags: u32,
    fe_reserved: [u32; 3],
}

/// The Fiemap object wraps the retrieval (via ioctl) of a file's underlying
/// extents on disk. Given a file, it will enumerate the areas of the underlying
/// partition where this file resides.
pub struct Fiemap {
    pub file_size: u64,
    pub extents: Vec<FiemapExtent>,
}

/// Sync data before creating the extent map.
static FIEMAP_FLAG_SYNC: u32 = 0x1;
// Map extended attribute tree.
//static FIEMAP_FLAG_XATTR: u32 = 0x2;

// The last extent in a file.
//static FIEMAP_EXTENT_LAST: u32 = 0x1;
/// Data location unknown.
static FIEMAP_EXTENT_UNKNOWN: u32 = 0x2;
/// Location still pending. Also sets FIEMAP_EXTENT_UNKNOWN.
static FIEMAP_EXTENT_DELALLOC: u32 = 0x4;
/// Data can not be read while the file system is unmounted.
static FIEMAP_EXTENT_ENCODED: u32 = 0x8;
/// Data is encrypted. Also sets EXTENT_NO_BYPASS.
static FIEMAP_EXTENT_DATA_ENCRYPTED: u32 = 0x80;
/// Extent offsets may not be block aligned.
static FIEMAP_EXTENT_ALIGNED: u32 = 0x100;
/// Data is mixed with metadata. Sets FIEMAP_EXTENT_NOT_ALIGNED.
static FIEMAP_EXTENT_INLINE: u32 = 0x200;
/// Multiple files in a block. Sets FIEMAP_EXTENT_NOT_ALIGNED.
static FIEMAP_EXTENT_TAIL: u32 = 0x400;
// Space is allocated, but no data is written.
//static FIEMAP_EXTENT_UNWRITTEN: u32 = 0x800;
// File does not natively support extents. Result merged for efficiency.
//static FIEMAP_EXTENT_MERGED: u32 = 0x1000;
/// Space shared with other files.
static FIEMAP_EXTENT_SHARED: u32 = 0x2000;

/// Define the mask of flags that would be bad to see on a file you plan on
/// operating on directly.
static FIEMAP_NO_RAW_ACCESS_FLAGS: u32 = FIEMAP_EXTENT_UNKNOWN
    | FIEMAP_EXTENT_DELALLOC
    | FIEMAP_EXTENT_ENCODED
    | FIEMAP_EXTENT_DATA_ENCRYPTED
    | FIEMAP_EXTENT_ALIGNED
    | FIEMAP_EXTENT_INLINE
    | FIEMAP_EXTENT_TAIL
    | FIEMAP_EXTENT_SHARED;

impl Fiemap {
    /// Create a new Fiemap object and run ioctls to load the fiemap for a given
    /// file. On success, returns a Fiemap object that encapsulates the extents
    /// for the file at the time this routine was run.
    pub fn new(source_file: &mut File) -> Result<Fiemap> {
        let file_size = source_file.metadata().unwrap().len();
        let extents = Fiemap::get_extents(source_file, 0, file_size, FIEMAP_FLAG_SYNC)?;
        debug!("File has {} extents:", extents.len());
        for extent in &extents {
            debug!(
                "logical {:x} physical {:x} len {:x} flags {:x}",
                extent.fe_logical, extent.fe_physical, extent.fe_length, extent.fe_flags
            );
            // If the extent has flags that wouldn't go well with direct access,
            // report that now and fail. "Unwritten" is acceptable if the file
            // is to be both written and read from underneath the file system.
            if (extent.fe_flags & FIEMAP_NO_RAW_ACCESS_FLAGS) != 0 {
                error!("File has bad flags {:x} for direct access. Extent logical {:x} physical {:x} len {:x}",
                       extent.fe_flags, extent.fe_logical, extent.fe_physical, extent.fe_length);
                return Err(HibernateError::InvalidFiemapError(format!(
                    "Fiemap extent has unexpected flags {:x}",
                    extent.fe_flags
                )))
                .context("Invalid fiemap");
            }
        }

        Ok(Fiemap { file_size, extents })
    }

    /// Return the extent corresponding to the given offset in the file.
    pub fn extent_for_offset(&self, offset: u64) -> Option<&FiemapExtent> {
        for extent in &self.extents {
            if (extent.fe_logical <= offset) && ((extent.fe_logical + extent.fe_length) > offset) {
                return Some(extent);
            }
        }

        None
    }

    /// Helper function to run the fiemap ioctl without any data to determine
    /// how many extent structures are needed. In a regular file, this count
    /// could go stale as soon as it is returned. This code assumes no other
    /// process is manipulating the file at the same time.
    fn get_extent_count(
        source_file: &mut File,
        fm_start: u64,
        fm_length: u64,
        fm_flags: u32,
    ) -> Result<u32> {
        let mut param = C_Fiemap {
            fm_start,
            fm_length,
            fm_flags,
            fm_mapped_extents: 0,
            fm_extent_count: 0,
            fm_reserved: 0,
        };

        // Safe because the param struct has been pre-initialized, uses repr(C),
        // and contains only basic types.
        let rc = unsafe {
            libc::ioctl(
                source_file.as_raw_fd(),
                FIEMAP,
                &mut param as *mut C_Fiemap as *mut c_void,
            )
        };

        if rc < 0 {
            return Err(HibernateError::FiemapError(sys_util::Error::last()))
                .context("Failed to get fiemap extent count");
        }

        Ok(param.fm_mapped_extents)
    }

    /// Execute the ioctl to get the extents, and convert them back to an array
    /// of extent structures.
    fn get_extents(
        source_file: &mut File,
        fm_start: u64,
        fm_length: u64,
        fm_flags: u32,
    ) -> Result<Vec<FiemapExtent>> {
        let extent_count = Fiemap::get_extent_count(source_file, fm_start, fm_length, fm_flags)?;
        let mut extents = vec![FiemapExtent::default(); extent_count as usize];
        let fiemap_len = mem::size_of::<C_Fiemap>();
        let extents_len = extents.len() * mem::size_of::<FiemapExtent>();
        let buffer_size = fiemap_len + extents_len;
        let mut fiemap = C_Fiemap {
            fm_start,
            fm_length,
            fm_flags,
            fm_mapped_extents: 0,
            fm_extent_count: extents.len() as u32,
            fm_reserved: 0,
        };

        let mut buffer = vec![0u8; buffer_size];
        // Copy the fiemap struct into the beginning of the u8 buffer. This is
        // safe because the buffer was allocated to be larger than this struct
        // size, and the structure contains no padding bytes.
        unsafe {
            let fiemap_slice = ::std::slice::from_raw_parts(
                (&fiemap as *const C_Fiemap) as *const u8,
                ::std::mem::size_of::<C_Fiemap>(),
            );
            buffer[0..fiemap_len].copy_from_slice(fiemap_slice);
        }

        // Safe because the ioctl operates on a buffer bounded by the length we
        // just supplied in fm_extent_count of the struct fiemap.
        let rc = unsafe {
            libc::ioctl(
                source_file.as_raw_fd(),
                FIEMAP,
                buffer.as_mut_ptr() as *mut _ as *mut c_void,
            )
        };

        if rc < 0 {
            return Err(HibernateError::FiemapError(sys_util::Error::last()))
                .context("Failed to get fiemap");
        }

        // Verify the ioctl returned the number of extents expected. This is
        // safe because the C_Fiemap is defined with the C convention, and all
        // members are basic basic types.
        unsafe {
            fiemap = std::ptr::read_unaligned(buffer[0..fiemap_len].as_ptr() as *const _);
        }

        if fiemap.fm_mapped_extents as usize != extents.len() {
            return Err(HibernateError::InvalidFiemapError(format!(
                "Got {} fiemap extents, expected {}",
                fiemap.fm_mapped_extents,
                extents.len()
            )))
            .context("Fiemap changed");
        }

        // Copy the extents returned from the ioctl out into the vector.
        for (i, extent) in extents.iter_mut().enumerate() {
            let start = fiemap_len + (i * mem::size_of::<FiemapExtent>());
            let end = start + mem::size_of::<FiemapExtent>();
            // This is safe because the ioctl returned this many fiemap_extents.
            // This copies from the u8 buffer back into safe (aligned) world.
            unsafe {
                *extent = std::ptr::read_unaligned(buffer[start..end].as_ptr() as *const _);
            }
        }

        Ok(extents)
    }
}
