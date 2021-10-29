// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements support for moving an image from one fd to another, with
//! alignment and transformation. This object represents the "motor" driving
//! data from one file descriptor to another.

use std::io::{IoSliceMut, Read, Write};

use anyhow::{Context, Result};
use libc::{self, off64_t};
use log::{debug, info, warn};

use crate::mmapbuf::MmapBuffer;

/// An ImageMover represents an engine used to move data from a source to a
/// destination. It provides alignment and batching, but does not do any
/// transformations to the data itself. Transformation objects can be hooked up
/// to the source and destination sides of it to form a sort of pipeline of
/// data, with this object as the active "pump".
pub struct ImageMover<'a> {
    source_file: &'a mut dyn Read,
    dest_file: &'a mut dyn Write,
    source_size: off64_t,
    bytes_done: off64_t,
    source_chunk: usize,
    dest_chunk: usize,
    buffer_size: usize,
    buffer: MmapBuffer,
    buffer_offset: usize,
    percent_reported: u32,
}

impl<'a> ImageMover<'a> {
    /// Create a new ImageMover object. The source_size parameter represents the
    /// total number of bytes to move. The source_chunk parameter represents the
    /// chunk size the mover should use when reading from the source. Similarly,
    /// the dest_chunk parameter represents the chunk size the mover should use
    /// when writing to the destination. If these are different, the image mover
    /// will batch reads or writes using an internal buffer.
    pub fn new(
        source_file: &'a mut dyn Read,
        dest_file: &'a mut dyn Write,
        source_size: off64_t,
        source_chunk: usize,
        dest_chunk: usize,
    ) -> Result<ImageMover<'a>> {
        // The buffer size is the max of the source or destination chunk size.
        // Both are expected to be powers of two, which means one is always a multiple
        // of the other.
        let buffer_size = std::cmp::max(source_chunk, dest_chunk);
        let buffer = MmapBuffer::new(buffer_size)?;
        Ok(Self {
            source_file,
            dest_file,
            source_size,
            bytes_done: 0,
            source_chunk,
            dest_chunk,
            buffer_size,
            buffer,
            buffer_offset: 0,
            percent_reported: 0,
        })
    }

    /// Write out the contents of the internal buffer to the destination, in
    /// chunks of dest_chunk size, regardless of whether or not the buffer has
    /// at least dest_chunk bytes.
    fn flush_buffer(&mut self) -> Result<()> {
        if self.buffer_offset == 0 {
            return Ok(());
        }

        let mut offset: usize = 0;
        while offset < self.buffer_offset {
            // Copy the remainder of the buffer, capped to the destination chunk size.
            let length = std::cmp::min(self.buffer_offset - offset, self.dest_chunk);
            let start = offset;
            let end = start + length;
            let buffer_slice = self.buffer.u8_slice();
            self.dest_file.write_all(&buffer_slice[start..end])?;
            offset += length;
            self.bytes_done += length as i64;
        }

        let percent_done = (self.bytes_done * 100 / self.source_size) as u32;
        if (percent_done / 10) != (self.percent_reported / 10) {
            info!(
                "Moved {}%, {}/{}",
                percent_done, self.bytes_done, self.source_size
            );
            self.percent_reported = percent_done;
        }

        self.buffer_offset = 0;
        Ok(())
    }

    /// Read a source sized chunk into the internal buffer. Write out any
    /// complete destination sized chunks.
    fn move_chunk(&mut self) -> Result<()> {
        // Move the whole rest of the image, capped to the source chunk size,
        // and capped to the remaining buffer space.
        let length = std::cmp::min(
            self.source_size - self.bytes_done - (self.buffer_offset as i64),
            self.source_chunk as i64,
        );
        let length = std::cmp::min(length as usize, self.buffer_size - self.buffer_offset);
        let start = self.buffer_offset;
        let end = start + length;
        let buffer_slice = self.buffer.u8_slice_mut();
        let mut slice_mut = [IoSliceMut::new(&mut buffer_slice[start..end])];
        let bytes_read = self
            .source_file
            .read_vectored(&mut slice_mut)
            .context("Failed to move image chunk")?;
        if bytes_read < length {
            warn!(
                "Only Read {}/{}, {}/{}",
                bytes_read, length, self.bytes_done, self.source_size
            );
        }

        self.buffer_offset += bytes_read;
        if self.buffer_offset >= self.buffer_size {
            self.flush_buffer()?;
        }

        Ok(())
    }

    /// Move the entire image from the source to the destination.
    pub fn move_all(&mut self) -> Result<()> {
        debug!("Moving image");
        while self.bytes_done + (self.buffer_offset as i64) < self.source_size {
            self.move_chunk()?;
        }

        self.flush_buffer()?;
        debug!("Finished moving image");
        Ok(())
    }
}
