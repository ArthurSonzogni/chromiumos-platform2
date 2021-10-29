// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages the "valid resume image" cookie.

use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;

use anyhow::{Context, Result};

use crate::hiberutil::{path_to_stateful_block, HibernateError};
use crate::mmapbuf::MmapBuffer;

/// The hibernate cookie is a flag stored at a known location on disk. The early
/// init scripts use this flag to determine whether or not to mount the stateful
/// partition in snapshot mode for resume, or normal read/write mode for a
/// traditional fresh boot. Normally this sort of cookie would be stored as a
/// regular file in the stateful partition itself. But we can't exactly do that
/// because this is the indicator used to determine _how_ to mount the RW
/// file systems.
///
/// This implementation currently stores the flag as a well-known string inside
/// the leftover space at the end of the sector containing the GPT header. This
/// space is ideal because its location is fixed, it's not manipulated in normal
/// circumstances, and the GPT header format is unlikely to change and start
/// using this space.
struct HibernateCookie {
    blockdev: File,
    buffer: MmapBuffer,
}

/// Define the size of the region we update.
const COOKIE_READ_SIZE: usize = 0x400;
const COOKIE_WRITE_SIZE: usize = 0x400;
/// Define the magic value the GPT stamps down, which we will use to verify
/// we're writing to an area that we expect. If somehow the world shifted out
/// from under us, this could prevent us from silently corrupting data.
const GPT_MAGIC_OFFSET: usize = 0x200;
const GPT_MAGIC: u64 = 0x5452415020494645; // 'EFI PART'

/// The beginning of the disk starts with a protective MBR, followed by a sector
/// just for the GPT header. The GPT header is quite small and doesn't use its
/// whole sector. Define the offset towards the end of the region where the
/// cookie will be written.
const COOKIE_MAGIC_OFFSET: usize = 0x3E0;
/// Define the magic token we write to indicate a valid hibernate partition.
/// This is both big (as in bigger than a single bit), and points the finger at
/// an obvious culprit, in the case this does end up unintentionally writing
/// over important data. This is made arbitrarily, but intentionally, to be 16
/// bytes.
const COOKIE_VALID_VALUE: &[u8] = b"HibernateCookie!";
/// Define a known "not valid" value as well. This is treated identically to
/// anything else that is invalid, but again could serve as a more useful
/// breadcrumb to someone debugging than 16 vanilla zeroes.
const COOKIE_POISON_VALUE: &[u8] = b"HibernateInvalid";
/// Define the size of the magic token, in bytes.
const COOKIE_SIZE: usize = 16;

impl HibernateCookie {
    /// Create a new HibernateCookie structure. This allocates resources but
    /// does not attempt to read or write the disk.
    pub fn new<P: AsRef<Path>>(path: P) -> Result<HibernateCookie> {
        let blockdev = OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(libc::O_DIRECT | libc::O_SYNC)
            .open(path)
            .context("Failed to open hibernate cookie")?;

        let buffer = MmapBuffer::new(COOKIE_READ_SIZE)?;
        Ok(HibernateCookie { blockdev, buffer })
    }

    /// Read the contents of the disk to determine if the cookie is set or not.
    /// On success, returns a boolean that is true if the hibernate cookie is
    /// set (indicating the on-disk file systems should not be altered).
    pub fn is_set(&mut self) -> Result<bool> {
        self.blockdev
            .seek(SeekFrom::Start(0))
            .context("Failed to seek in hibernate cookie")?;
        let buffer_slice = self.buffer.u8_slice_mut();
        self.blockdev
            .read_exact(&mut buffer_slice[..COOKIE_READ_SIZE])
            .context("Failed to read hibernate cookie")?;

        // Verify there's a GPT header magic where there should be one.
        // This would catch cases like writing to the wrong place or the
        // GPT layout/location changing. This might need enlightenment for a
        // disk with 4kb blocks, this check will let us know that too.
        let gpt_sig_offset = GPT_MAGIC_OFFSET;
        let gpt_sig_offset_end = gpt_sig_offset + 8;
        let mut gpt_sig = [0u8; 8];
        let buffer_slice = self.buffer.u8_slice();
        gpt_sig.copy_from_slice(&buffer_slice[gpt_sig_offset..gpt_sig_offset_end]);
        let gpt_sig = u64::from_le_bytes(gpt_sig);
        if gpt_sig != GPT_MAGIC {
            return Err(HibernateError::CookieError(format!(
                "GPT magic not found: {:x?}",
                gpt_sig
            )))
            .context("Failed to verify GPT magic");
        }

        let magic_start = COOKIE_MAGIC_OFFSET;
        let magic_end = magic_start + COOKIE_SIZE;
        let equal = buffer_slice[magic_start..magic_end] == *COOKIE_VALID_VALUE;
        Ok(equal)
    }

    /// Write the hibernate cookie to disk via a fresh read modify write
    /// operation. The valid parameter indicates whether to write a valid
    /// hibernate cookie (true, indicating on-disk file systems should be
    /// altered), or poison value (false, indicating no impending hibernate
    /// resume, file systems can be mounted RW).
    pub fn write(&mut self, valid: bool) -> Result<()> {
        let existing = self.is_set()?;
        self.blockdev
            .seek(SeekFrom::Start(0))
            .context("Failed to seek hibernate cookie")?;
        if valid == existing {
            return Ok(());
        }

        let magic_start = COOKIE_MAGIC_OFFSET;
        let magic_end = magic_start + COOKIE_SIZE;
        let cookie = if valid {
            COOKIE_VALID_VALUE
        } else {
            COOKIE_POISON_VALUE
        };

        let buffer_slice = self.buffer.u8_slice_mut();
        buffer_slice[magic_start..magic_end].copy_from_slice(cookie);
        let end = COOKIE_WRITE_SIZE;
        self.blockdev
            .write_all(&buffer_slice[..end])
            .context("Failed to write hibernate cookie")?;

        self.blockdev
            .flush()
            .context("Failed to flush hibernate cookie")?;

        self.blockdev
            .sync_all()
            .context("Failed to sync hibernate cookie")?;
        Ok(())
    }
}

/// Public function to read the hibernate cookie and return whether or not it is
/// set. The optional path parameter contains the path to the disk to examine.
/// If not supplied, the boot disk will be examined.
pub fn get_hibernate_cookie<P: AsRef<Path>>(path_str: Option<P>) -> Result<bool> {
    let mut cookie = open_hibernate_cookie(path_str)?;
    cookie.is_set()
}

/// Public function to set the hibernate cookie value. The valid parameter, if
/// true, indicates that upon the next boot file systems should not be altered
/// on disk, since there's a valid resume image. The optional path parameter
/// contains the path to the disk to examine.
pub fn set_hibernate_cookie<P: AsRef<Path>>(path: Option<P>, valid: bool) -> Result<()> {
    let mut cookie = open_hibernate_cookie(path)?;
    cookie.write(valid)
}

fn open_hibernate_cookie<P: AsRef<Path>>(path_ref: Option<P>) -> Result<HibernateCookie> {
    if let Some(path) = path_ref {
        HibernateCookie::new(path)
    } else {
        HibernateCookie::new(path_to_stateful_block()?)
    }
}
