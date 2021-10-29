// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement support for loading an image from disk and holding it in memory.
//! This is used to frontload potentially slow disk operations while we're stuck
//! waiting for user input anyway, but not yet capable of decrypting.

use std::collections::VecDeque;
use std::io::{Error as IoError, ErrorKind, IoSliceMut, Read};

use anyhow::{Context, Result};
use log::{debug, info, warn};

use crate::hiberutil::{get_available_pages, get_page_size, get_total_memory_pages};
use crate::mmapbuf::MmapBuffer;

/// Allocate buffers in chunks to keep things large but manageable.
const PRELOADER_CHUNK_SIZE: usize = 1024 * 1024 * 2;
/// The minimum percent of memory to keep free.
const RESERVE_MEMORY_PERCENT: usize = 6;

/// The ImagePreloader can read data from a source file and hold it in a
/// list of buffers. It then implements the Read trait, which drains those
/// buffers.
pub struct ImagePreloader<'a> {
    source: &'a mut dyn Read,
    chunks: VecDeque<ImageChunk>,
    total_size: usize,
    size_loaded: usize,
    chunk_offset: usize,
}

impl<'a> ImagePreloader<'a> {
    /// Create a new preloader, with a given total image size, and no buffers
    /// initially.
    pub fn new(source: &'a mut dyn Read, total_size: usize) -> Self {
        Self {
            source,
            chunks: VecDeque::new(),
            total_size,
            size_loaded: 0,
            chunk_offset: 0,
        }
    }

    /// Allocate a new chunk, and fill it with source file material. On success,
    /// returns a boolean indicating if all chunks have been read. The chunk
    /// size is currently hardcoded.
    pub fn load_next_chunk(&mut self) -> Result<bool> {
        if self.size_loaded >= self.total_size {
            return Ok(true);
        }

        let chunk_size = std::cmp::min(PRELOADER_CHUNK_SIZE, self.total_size - self.size_loaded);
        let chunk = ImageChunk::new(self.source, chunk_size)?;
        self.size_loaded += chunk.size;
        if chunk.size == 0 {
            return Ok(true);
        }

        self.chunks.push_back(chunk);
        Ok(self.size_loaded >= self.total_size)
    }

    /// Load as many image chunks as possible without exhausting system memory.
    pub fn load_into_available_memory(&mut self) -> Result<()> {
        let total_pages = get_total_memory_pages();
        let minimum_pages = total_pages * RESERVE_MEMORY_PERCENT / 100;
        debug!(
            "System has {} pages, preloading until there are {}",
            total_pages, minimum_pages
        );
        loop {
            // Load a chunk, or stop if all chunks are loaded.
            if self.load_next_chunk()? {
                debug!(
                    "Preloaded entire image, still {} pages available.",
                    get_available_pages()
                );
                break;
            }

            let available_pages = get_available_pages();
            if available_pages <= minimum_pages {
                info!(
                    "Preloaded {}MB, leaving {}MB free memory",
                    self.size_loaded / 1024 / 1024,
                    get_available_pages() * get_page_size() / 1024 / 1024
                );
                break;
            }
        }

        Ok(())
    }
}

impl Read for ImagePreloader<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let mut offset = 0usize;
        let length = buf.len();
        while offset < length {
            // Load another chunk if the list is empty.
            // Break out if it's the end.
            if self.chunks.is_empty() {
                let finished = self.load_next_chunk().map_err(|e| {
                    IoError::new(ErrorKind::InvalidInput, format!("I/O error: {}", e))
                })?;

                if finished {
                    break;
                }
            }

            let chunk = self.chunks.front().unwrap();

            debug_assert!(self.chunk_offset < chunk.size);

            let this_io_length = std::cmp::min(chunk.size - self.chunk_offset, length - offset);
            let buffer_slice = chunk.buffer.u8_slice();
            let dst_end = offset + this_io_length;
            let chunk_start = self.chunk_offset;
            let chunk_end = chunk_start + this_io_length;
            buf[offset..dst_end].copy_from_slice(&buffer_slice[chunk_start..chunk_end]);

            // Advance the position within the chunk.
            self.chunk_offset += this_io_length;

            debug_assert!(self.chunk_offset <= chunk.size);

            // If this chunk is fully consumed, pop it off the list and let the
            // buffer go free.
            if self.chunk_offset >= chunk.size {
                self.chunk_offset = 0;
                self.chunks.pop_front();
            }

            offset += this_io_length;
        }

        Ok(offset)
    }
}

/// Internal structure storing an individual buffer of the ImagePreloader.
struct ImageChunk {
    pub buffer: MmapBuffer,
    pub size: usize,
}

impl ImageChunk {
    /// Create and initialize from the given source a new buffer of the
    /// specified size.
    pub fn new(source: &mut dyn Read, size: usize) -> Result<Self> {
        let mut buffer = MmapBuffer::new(size)?;
        let buffer_slice = buffer.u8_slice_mut();
        let mut slice_mut = [IoSliceMut::new(&mut buffer_slice[..size])];
        let bytes_read = source
            .read_vectored(&mut slice_mut)
            .context("Failed to read image chunk")?;
        if bytes_read < size {
            warn!("Only Read {}/{}", bytes_read, size);
        }

        Ok(ImageChunk {
            buffer,
            size: bytes_read,
        })
    }
}
