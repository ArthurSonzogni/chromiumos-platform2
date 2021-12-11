// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement support for accessing file contents directly via the underlying block device.

use std::fs::{File, OpenOptions};
use std::io::{Error as IoError, ErrorKind, IoSlice, IoSliceMut, Read, Seek, SeekFrom, Write};
use std::os::unix::fs::OpenOptionsExt;

use anyhow::{Context, Result};
use log::{debug, error};

use crate::fiemap::{Fiemap, FiemapExtent};
use crate::hiberutil::{get_page_size, path_to_stateful_part};
use crate::mmapbuf::MmapBuffer;

/// The BouncedDiskFile is a convencience wrapper around the DiskFile structure.
/// It uses an internal buffer to avoid the stricter buffer alignment
/// requirements associated with raw DiskFile access. Think of it as a more
/// convenient, but slightly slower equivalent to the DiskFile.
pub struct BouncedDiskFile {
    disk_file: DiskFile,
    buffer: MmapBuffer,
}

impl BouncedDiskFile {
    /// Create a new BouncedDiskFile object.
    pub fn new(fs_file: &mut File, block_file: Option<File>) -> Result<BouncedDiskFile> {
        let page_size = get_page_size();
        Ok(BouncedDiskFile {
            disk_file: DiskFile::new(fs_file, block_file)?,
            buffer: MmapBuffer::new(page_size)?,
        })
    }

    /// Enable or disable logging on this file. Logging should be disabled if
    /// this file is serving the log itself, otherwise a deadlock occurs.
    pub fn set_logging(&mut self, enable: bool) {
        self.disk_file.set_logging(enable)
    }

    /// Sync file contents.
    pub fn sync_all(&self) -> std::io::Result<()> {
        self.disk_file.sync_all()
    }

    /// Convenience method to reset the seek position back to the start of the
    /// file.
    pub fn rewind(&mut self) -> Result<()> {
        self.disk_file.rewind()
    }
}

impl Read for BouncedDiskFile {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let mut offset = 0usize;
        let length = buf.len();
        while offset < length {
            let size_this_round = std::cmp::min(self.buffer.len(), length - offset);

            // Read into the aligned buffer.
            let src_end = size_this_round;
            let buffer_slice = self.buffer.u8_slice_mut();
            let mut slice = [IoSliceMut::new(&mut buffer_slice[..src_end])];
            let bytes_done = self.disk_file.read_vectored(&mut slice)?;
            if bytes_done == 0 {
                break;
            }

            // Copy into the caller's buffer.
            let dst_end = offset + bytes_done;
            buf[offset..dst_end].copy_from_slice(&buffer_slice[..bytes_done]);
            offset += bytes_done;
        }

        Ok(offset)
    }
}

impl Write for BouncedDiskFile {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let mut offset = 0usize;
        let length = buf.len();
        while offset < length {
            let size_this_round = std::cmp::min(self.buffer.len(), length - offset);

            // Copy into the aligned buffer.
            let src_end = offset + size_this_round;
            let buffer_slice = self.buffer.u8_slice_mut();
            buffer_slice[..size_this_round].copy_from_slice(&buf[offset..src_end]);

            // Do the write.
            let slice = [IoSlice::new(&buffer_slice[..size_this_round])];
            let bytes_done = self.disk_file.write_vectored(&slice)?;
            if bytes_done == 0 {
                break;
            }

            offset += bytes_done;
        }

        Ok(offset)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.disk_file.flush()
    }
}

impl Seek for BouncedDiskFile {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        self.disk_file.seek(pos)
    }
}

/// A DiskFile can take in a preallocated file and read or write to it by
/// accessing the file blocks on disk directly. In the cases we use, the file
/// has its disk extents fully allocated, but they're all set as
/// "uninitialized", meaning this effectively writes underneath the file system
/// data. We are effectively using the file system as a "disk area reservation"
/// system, in the absence of a dedicated hibernate partition. Operations are
/// not buffered, and may have alignment requirements depending on whether or
/// not the underlying block device was opened with O_DIRECT or not.
pub struct DiskFile {
    fiemap: Fiemap,
    blockdev: File,
    current_position: u64,
    current_extent: FiemapExtent,
    logging: bool,
}

impl DiskFile {
    /// Create a new DiskFile structure, given an open file in the file system
    /// (whose extents should be accessed directly), and the underlying block
    /// device of that file. If no block devices is given, the stateful partition
    /// is located and used.
    pub fn new(fs_file: &mut File, block_file: Option<File>) -> Result<DiskFile> {
        let fiemap = Fiemap::new(fs_file)?;
        let blockdev = match block_file {
            None => {
                let blockdev_path = path_to_stateful_part()?;
                debug!("Found hibernate block device: {}", blockdev_path);
                OpenOptions::new()
                    .read(true)
                    .write(true)
                    .custom_flags(libc::O_DIRECT)
                    .open(&blockdev_path)
                    .context("Failed to open disk file block device")?
            }
            Some(f) => f,
        };

        // This is safe because a zeroed extent is valid.
        let mut disk_file = unsafe {
            DiskFile {
                fiemap,
                blockdev,
                current_position: 0,
                current_extent: std::mem::zeroed(),
                logging: true,
            }
        };

        // Seek to the start of the file so the current_position is always valid.
        disk_file
            .seek(SeekFrom::Start(0))
            .context("Failed to do initial seek")?;

        Ok(disk_file)
    }

    /// Enable or disable logging coming from this DiskFile object. Logging
    /// should be disabled on the file backing logging itself, otherwise a
    /// logging deadlock results.
    pub fn set_logging(&mut self, enable: bool) {
        self.logging = enable;
    }

    /// Sync the underlying block device.
    pub fn sync_all(&self) -> std::io::Result<()> {
        self.blockdev.sync_all()
    }

    /// Convenience method to reset the file position back to the start of the file.
    pub fn rewind(&mut self) -> Result<()> {
        self.seek(SeekFrom::Start(0))
            .context("Failed to rewind disk file")?;
        Ok(())
    }

    /// Helper function to determine whether the current position has valid
    /// bytes ahead of it within the current extent. If false, it indicates an
    /// internal seek needs to be done.
    fn current_position_valid(&self) -> bool {
        let start = self.current_extent.fe_logical;
        let end = start + self.current_extent.fe_length;
        (self.current_position >= start) && (self.current_position < end)
    }
}

impl Drop for DiskFile {
    fn drop(&mut self) {
        if self.logging {
            debug!(
                "Dropping {} MB DiskFile",
                self.fiemap.file_size / 1024 / 1024
            );
        }

        if let Err(e) = self.sync_all() {
            if self.logging {
                error!("Error syncing DiskFile: {}", e);
            }
        }
    }
}

impl Read for DiskFile {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let mut offset = 0usize;
        let length = buf.len();
        while offset < length {
            // There is no extending the file size.
            if self.current_position >= self.fiemap.file_size {
                break;
            }

            // Ensure the block device is seeked to the right position.
            if !self.current_position_valid() {
                self.seek(SeekFrom::Current(0))?;
            }

            // Get the offset within the current extent.
            let delta = self.current_position - self.current_extent.fe_logical;
            // Get the size remaining to be read or written in this extent.
            let extent_remaining = self.current_extent.fe_length - delta;
            // Get the minimum of the remaining input buffer or the remaining extent.
            let this_io_length = std::cmp::min((length - offset) as u64, extent_remaining) as usize;

            // Get a slice of the portion of the buffer to be read into, and read from
            // the block device into the slice.
            let end = offset + this_io_length;
            self.blockdev.read_exact(&mut buf[offset..end])?;
            self.current_position += this_io_length as u64;
            offset += this_io_length;
        }

        Ok(offset)
    }
}

impl Write for DiskFile {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let mut offset = 0usize;
        let length = buf.len();
        while offset < length {
            // There is no extending the file size.
            if self.current_position >= self.fiemap.file_size {
                error!(
                    "DiskFile EOF: {}/{} written, file size {}",
                    offset, length, self.fiemap.file_size
                );
                break;
            }

            // Ensure the block device is seeked to the right position.
            if !self.current_position_valid() {
                self.seek(SeekFrom::Current(0))?;
            }

            // Get the offset within the current extent.
            let delta = self.current_position - self.current_extent.fe_logical;
            // Get the size remaining to be read or written in this extent.
            let extent_remaining = self.current_extent.fe_length - delta;
            // Get the minimum of the remaining input buffer or the remaining extent.
            let this_io_length = std::cmp::min((length - offset) as u64, extent_remaining) as usize;

            // Get a slice of the portion of the buffer to be read into, and read from
            // the block device into the slice.
            let end = offset + this_io_length;
            self.blockdev.write_all(&buf[offset..end])?;
            self.current_position += this_io_length as u64;
            offset += this_io_length;
        }

        Ok(offset)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.blockdev.flush()
    }
}

impl Seek for DiskFile {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        let mut pos = match pos {
            SeekFrom::Start(p) => p as i64,
            SeekFrom::End(p) => self.fiemap.file_size as i64 + p,
            SeekFrom::Current(p) => self.current_position as i64 + p,
        };

        if pos < 0 {
            return Err(IoError::new(ErrorKind::InvalidInput, "Negative seek"));
        }

        if pos > self.fiemap.file_size as i64 {
            pos = self.fiemap.file_size as i64;
        }

        let new_position = pos as u64;
        let extent = match self.fiemap.extent_for_offset(new_position) {
            None => {
                return Err(IoError::new(
                    ErrorKind::InvalidInput,
                    "No extent for position",
                ))
            }
            Some(e) => *e,
        };

        let delta = new_position - extent.fe_logical;
        let block_offset = extent.fe_physical + delta;
        if self.logging {
            debug!("Seeking to {:x}", block_offset);
        }

        let seeked_offset = self.blockdev.seek(SeekFrom::Start(block_offset))?;
        if seeked_offset != block_offset {
            return Err(IoError::new(ErrorKind::Other, "Failed to seek DiskFile"));
        }

        self.current_position = new_position;
        self.current_extent = extent;
        Ok(new_position)
    }
}
