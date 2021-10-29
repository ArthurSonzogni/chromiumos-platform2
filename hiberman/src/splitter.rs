// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Splits the hibernate image into header and data. Splitting is needed in
//! order to make image preloading work properly. The way the kernel does resume
//! is to read in the header pages, then attempt to allocate all the space it
//! needs for the hibernate image all at once. (This is a good idea because for
//! any page it gets in this big allocation that's also used by the hibernated
//! kernel, data can be restored directly to the correct pfn without a second
//! copy.) What this means is if you try to preload the hibernate image into
//! memory before feeding the header portion into the kernel, the kernel's giant
//! allocation attempt ends up failing or taking the system down. The splitter
//! allows us to save and restore the header portion separately (and
//! unencrypted), so that we can feed the header portion in early. This lets the
//! kernel have its big chunk, and then we can soak up the rest of memory
//! preloading. The header portion is ok to save unencrypted because it's
//! essentially just a page list. The kernel validates its contents, and we also
//! verify its hash to detect tampering.

use std::convert::TryInto;
use std::io::{Error as IoError, ErrorKind, Read, Write};

use anyhow::{Context, Result};
use libc::utsname;
use log::debug;
use openssl::hash::{Hasher, MessageDigest};

use crate::hibermeta::{HibernateMetadata, META_HASH_SIZE};
use crate::hiberutil::{get_page_size, HibernateError};

/// A machine with 32GB RAM has 8M PFNs. Half of that times 8 bytes per PFN is
/// 32MB.
pub const HIBER_HEADER_MAX_SIZE: i64 = (1024 * 1024 * 32) + 4096;

/// Define the swsusp_info header created by the kernel at the start of each
/// hibernate image. Use this to figure out how many header pages there are.
#[repr(C)]
struct SwSuspInfo {
    uts: utsname,
    version_code: u32,
    num_physpages: usize,
    cpus: u32,
    image_pages: usize,
    pages: usize,
    size: usize,
}

/// An image splitter is a generic object that implements the Write trait. It
/// will divert writes first into the header file, and then the data file.
pub struct ImageSplitter<'a> {
    header_file: &'a mut dyn Write,
    data_file: &'a mut dyn Write,
    metadata: &'a mut HibernateMetadata,
    page_size: usize,
    meta_pages: usize,
    pages_done: usize,
    hasher: Hasher,
}

/// The ImageSplitter routes an initial set of writes to a header file, then
/// routes the main data to the data file. It uses the first sector's write to
/// determine where to make the split. It also computes the hash of the header
/// portion as it goes by, and saves that hash into the metadata.
impl<'a> ImageSplitter<'a> {
    /// Create a new image splitter, given pointers to the header destination
    /// and data file destination. The metadata is also received as a place to
    /// store the header hash.
    pub fn new(
        header_file: &'a mut dyn Write,
        data_file: &'a mut dyn Write,
        metadata: &'a mut HibernateMetadata,
    ) -> ImageSplitter<'a> {
        Self {
            header_file,
            data_file,
            metadata,
            page_size: get_page_size(),
            meta_pages: 0,
            pages_done: 0,
            hasher: Hasher::new(MessageDigest::sha256()).unwrap(),
        }
    }

    /// Helper function to write contents to the header file, snarfing out the
    /// header data size on the way down. Any remainder is passed to the data
    /// file.
    fn write_header(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let page_size = self.page_size;
        let length = buf.len();

        // The write is always expected to be at least a page, and an even
        // multiple of a page.
        assert!(length >= page_size);
        assert!((length & (page_size - 1)) == 0);

        // If this is the first page, snarf the header out of the buffer to
        // figure out the number of metadata pages.
        if self.pages_done == 0 {
            assert!(self.meta_pages == 0);

            self.meta_pages = get_meta_page_count(buf, page_size)?;
            // Save the non-data page count in the official metadata too.
            self.metadata.pagemap_pages =
                self.meta_pages.try_into().expect("Too many metadata pages");
        }

        // Write out to the header if that's not done yet.
        let mut offset = 0;
        if self.pages_done < self.meta_pages {
            // Send either the rest of the metadata or the rest of this buffer.
            let mut meta_size = (self.meta_pages - self.pages_done) * self.page_size;
            if meta_size > length {
                meta_size = length;
            }

            self.hasher.update(&buf[..meta_size]).unwrap();
            let bytes_written = self.header_file.write(&buf[..meta_size])?;

            // Assert that the write did not cross into data territory, only sidled
            // up to it.
            assert!((self.pages_done + (bytes_written / page_size)) <= self.meta_pages);
            assert!((bytes_written & (page_size - 1)) == 0);

            self.pages_done += bytes_written / page_size;
            // If the header just finished, finalize the hash of it and save it into
            // the metadata.
            if self.pages_done == self.meta_pages {
                self.metadata
                    .header_hash
                    .copy_from_slice(&self.hasher.finish().unwrap());
            }

            if bytes_written == length {
                return Ok(bytes_written);
            }

            offset = bytes_written;
        }

        // Write down to the data file. If this is the first byte of data, save
        // it off to the metadata too.
        if self.pages_done == self.meta_pages {
            self.metadata.first_data_byte = buf[offset];
        }

        // Send the rest of the write down to the data file.
        let bytes_written = self.data_file.write(&buf[offset..])?;

        assert!((bytes_written & (page_size - 1)) == 0);

        self.pages_done += bytes_written / page_size;
        offset += bytes_written;
        Ok(offset)
    }
}

impl Write for ImageSplitter<'_> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        // Just forward the write on down if the header's already been split
        // off. This will be the hot path. The comparison is strictly greater
        // than because we also need to slurp the first data byte from the first
        // data page.
        if (self.meta_pages != 0) && (self.pages_done > self.meta_pages) {
            return self.data_file.write(buf);
        }

        self.write_header(buf)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.header_file.flush()?;
        self.data_file.flush()
    }
}

/// An ImageJoiner is the opposite of an ImageSplitter. It implements Read,
/// stitching together the header contents followed by the data contents.
pub struct ImageJoiner<'a> {
    header_file: &'a mut dyn Read,
    data_file: &'a mut dyn Read,
    page_size: usize,
    meta_pages: usize,
    pages_done: usize,
    hasher: Hasher,
    header_hash: Vec<u8>,
}

impl<'a> ImageJoiner<'a> {
    /// Create a new ImageJoiner, a single object implementing the Read trait
    /// that will stitch together the header, followed by the data portion.
    pub fn new(header_file: &'a mut dyn Read, data_file: &'a mut dyn Read) -> ImageJoiner<'a> {
        Self {
            header_file,
            data_file,
            page_size: get_page_size(),
            meta_pages: 0,
            pages_done: 0,
            hasher: Hasher::new(MessageDigest::sha256()).unwrap(),
            header_hash: vec![],
        }
    }

    /// Returns the computed hash of the header region, which the caller will
    /// compare to what's in the private metadata (once that's decrypted and
    /// available).
    pub fn get_header_hash(&self, hash: &mut [u8; META_HASH_SIZE]) -> Result<usize> {
        if self.header_hash.len() != META_HASH_SIZE {
            return Err(HibernateError::HeaderIncomplete())
                .context("The header is invalid or has not yet been read");
        }

        hash.copy_from_slice(&self.header_hash[..]);
        Ok(self.meta_pages)
    }

    /// Helper function to read contents from the header file, snarfing out the
    /// header data size on the way. Any remaining reads are fed from the data
    /// file.
    fn read_header(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let page_size = self.page_size;
        let mut offset = 0usize;
        let length = buf.len();

        // The read better be at least a page, and an even multiple of a page.
        assert!(length >= page_size);
        assert!((length & (page_size - 1)) == 0);

        // If this is the first read, do a single page read to snarf out the
        // header. This also ensures the header isn't over-read.
        if self.pages_done == 0 {
            assert!(self.meta_pages == 0);

            let bytes_read = self.header_file.read(&mut buf[..page_size])?;

            assert!(bytes_read == page_size);

            self.hasher.update(&buf[..bytes_read]).unwrap();
            offset += bytes_read;
            self.pages_done += bytes_read / page_size;
            self.meta_pages = get_meta_page_count(buf, page_size)?;
        }

        // Read the rest of the header file (or at least the rest of the buffer).
        let mut meta_size = (self.meta_pages - self.pages_done) * page_size;
        if meta_size > (length - offset) {
            meta_size = length - offset;
        }

        let end = offset + meta_size;
        let bytes_read = self.header_file.read(&mut buf[offset..end])?;

        assert!((bytes_read & (page_size - 1)) == 0);

        let read_end = offset + bytes_read;
        self.hasher.update(&buf[offset..read_end]).unwrap();
        self.pages_done += bytes_read / page_size;

        assert!(self.pages_done <= self.meta_pages);

        // Save the hash locally if this was the last header page. The caller
        // will eventually ask for it once the private metadata is unlocked.
        if self.pages_done == self.meta_pages {
            let hash_slice: &[u8] = &self.hasher.finish().unwrap();
            self.header_hash = hash_slice.to_vec();
        }

        offset += bytes_read;
        if offset == length {
            return Ok(offset);
        }

        // Send the remaining read down to the data file.
        let bytes_read = self.data_file.read(&mut buf[offset..])?;
        Ok(offset + bytes_read)
    }
}

impl Read for ImageJoiner<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        // Just forward the read on down if the header's already been split off.
        // This will be the hot path.
        if (self.meta_pages != 0) && (self.pages_done >= self.meta_pages) {
            return self.data_file.read(buf);
        }

        self.read_header(buf)
    }
}

/// Read the header out of the first page of the hibernate image. Returns the
/// number of metadata pages on success.
fn get_meta_page_count(buf: &[u8], page_size: usize) -> std::io::Result<usize> {
    assert!(buf.len() >= page_size);

    // Assert that libc didn't somehow change the size of the utsname header
    // while we were sleeping, which would ruin the other structure member
    // offsets.
    assert!(std::mem::size_of::<utsname>() == (65 * 6));

    // This is safe because the buffer is larger than the structure size, and
    // the types in the struct are all basic.
    let header: SwSuspInfo = unsafe {
        std::ptr::read_unaligned(buf[0..std::mem::size_of::<SwSuspInfo>()].as_ptr() as *const _)
    };

    let meta_pages = header.pages - header.image_pages;
    debug!(
        "Image has {:x?} image pages, {:x?} header pages",
        header.image_pages,
        header.pages - header.image_pages
    );

    if header.pages < header.image_pages {
        return Err(IoError::new(
            ErrorKind::InvalidInput,
            format!(
                "Header pages {:x} < image_pages {:x}",
                header.pages, header.image_pages
            ),
        ));
    }

    // TODO: Validate additional fields in the structure in case the kernel
    // changes the format, like num_physpages.
    if (meta_pages * page_size) as i64 > HIBER_HEADER_MAX_SIZE {
        return Err(IoError::new(
            ErrorKind::InvalidInput,
            format!(
                "Too many header pages. Pages {:x}, image_pages {:x}",
                header.pages, header.image_pages
            ),
        ));
    }

    if meta_pages < 2 {
        return Err(IoError::new(
            ErrorKind::InvalidInput,
            format!(
                "Too few header pages. Pages {:x}, image_pages {:x}",
                header.pages, header.image_pages
            ),
        ));
    }

    Ok(meta_pages)
}
