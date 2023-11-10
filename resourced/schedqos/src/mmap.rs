// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io;
use std::num::NonZeroUsize;
use std::ops::Deref;
use std::ops::DerefMut;
use std::os::fd::AsRawFd;

pub struct Mmap {
    file: File,
    ptr: *mut u8,
    /// size must be non-zero because zero as old_size on mremap(2) has special meaning.
    size: NonZeroUsize,
}

/// SAFETY: [Mmap] is safe to [Send] because the internal `ptr` is accessed by [Mmap] only.
unsafe impl Send for Mmap {}

impl Mmap {
    pub fn new(file: File, size: NonZeroUsize) -> io::Result<Self> {
        file.set_len(size.get() as u64)?;
        // SAFETY: Memory space managed by Rust is not modified.
        let ptr = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                size.get(),
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                file.as_raw_fd(),
                0,
            )
        } as *mut u8;
        if ptr == libc::MAP_FAILED as *mut u8 {
            Err(io::Error::last_os_error())
        } else {
            Ok(Self { file, ptr, size })
        }
    }
}

impl Mmap {
    pub fn resize(&mut self, new_size: NonZeroUsize) -> io::Result<()> {
        self.file.set_len(new_size.get() as u64)?;
        // SAFETY: Memory space managed by Rust is not modified.
        let ptr = unsafe {
            libc::mremap(
                self.ptr as *mut libc::c_void,
                self.size.get(),
                new_size.get(),
                libc::MAP_SHARED,
            )
        } as *mut u8;
        if ptr.is_null() {
            return Err(io::Error::last_os_error());
        }
        self.ptr = ptr;
        self.size = new_size;
        Ok(())
    }
}

impl Deref for Mmap {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        // SAFETY: ptr and size is guaranteed to be valid.
        // SAFETY: lifetime is the same as self.
        unsafe { std::slice::from_raw_parts(self.ptr, self.size.get()) }
    }
}

impl DerefMut for Mmap {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: ptr and size is guaranteed to be valid.
        // SAFETY: lifetime is the same as self.
        unsafe { std::slice::from_raw_parts_mut(self.ptr, self.size.get()) }
    }
}

impl Drop for Mmap {
    fn drop(&mut self) {
        // SAFETY: ptr and size is guaranteed to be valid.
        let res = unsafe { libc::munmap(self.ptr as *mut libc::c_void, self.size.get()) };
        if res != 0 {
            panic!("munmap failed: {}", io::Error::last_os_error());
        }
    }
}

#[cfg(test)]
mod tests {
    use std::io::Write;

    use super::*;

    #[test]
    fn test_mmap() {
        let mut file = tempfile::NamedTempFile::new().unwrap();
        file.write_all(&[1; 2048]).unwrap();
        let mut mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(4096).unwrap()).unwrap();
        assert_eq!(mmap.len(), 4096);
        assert_eq!(file.as_file().metadata().unwrap().len(), 4096);
        assert_eq!(&mmap[0..2048], &[1; 2048]);
        assert_eq!(&mmap[2048..4096], &[0; 2048]);
        mmap[3072..4096].copy_from_slice(&[2; 1024]);
        assert_eq!(&mmap[3072..4096], &[2; 1024]);

        // munmap(2) succeeds.
        drop(mmap);

        let mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(8192).unwrap()).unwrap();
        assert_eq!(mmap.len(), 8192);
        assert_eq!(file.as_file().metadata().unwrap().len(), 8192);
        assert_eq!(&mmap[0..2048], &[1; 2048]);
        assert_eq!(&mmap[2048..3072], &[0; 1024]);
        assert_eq!(&mmap[3072..4096], &[2; 1024]);
        assert_eq!(&mmap[4096..8192], &[0; 4096]);
    }

    #[test]
    fn test_mmap_non_page_size() {
        let mut file = tempfile::NamedTempFile::new().unwrap();
        file.write_all(&[1; 10]).unwrap();
        let mut mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(20).unwrap()).unwrap();
        assert_eq!(mmap.len(), 20);
        assert_eq!(file.as_file().metadata().unwrap().len(), 20);
        assert_eq!(&mmap[0..10], &[1; 10]);
        assert_eq!(&mmap[10..20], &[0; 10]);
        mmap[15..20].copy_from_slice(&[2; 5]);
        assert_eq!(&mmap[15..20], &[2; 5]);

        // munmap(2) succeeds.
        drop(mmap);

        let mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(40).unwrap()).unwrap();
        assert_eq!(mmap.len(), 40);
        assert_eq!(file.as_file().metadata().unwrap().len(), 40);
        assert_eq!(&mmap[0..10], &[1; 10]);
        assert_eq!(&mmap[10..15], &[0; 5]);
        assert_eq!(&mmap[15..20], &[2; 5]);
        assert_eq!(&mmap[20..40], &[0; 20]);
    }

    #[test]
    fn test_resize_extend() {
        let mut file = tempfile::NamedTempFile::new().unwrap();
        file.write_all(&[1; 2048]).unwrap();
        let mut mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(4096).unwrap()).unwrap();
        assert_eq!(mmap.len(), 4096);
        assert_eq!(file.as_file().metadata().unwrap().len(), 4096);
        mmap[3072..4096].copy_from_slice(&[2; 1024]);

        mmap.resize(NonZeroUsize::new(8192).unwrap()).unwrap();
        assert_eq!(mmap.len(), 8192);
        assert_eq!(file.as_file().metadata().unwrap().len(), 8192);
        assert_eq!(&mmap[4096..8192], &[0; 4096]);
        mmap[4096..8192].copy_from_slice(&[3; 4096]);

        // munmap(2) succeeds.
        drop(mmap);

        let mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(8192).unwrap()).unwrap();
        assert_eq!(mmap.len(), 8192);
        assert_eq!(file.as_file().metadata().unwrap().len(), 8192);
        assert_eq!(&mmap[0..2048], &[1; 2048]);
        assert_eq!(&mmap[2048..3072], &[0; 1024]);
        assert_eq!(&mmap[3072..4096], &[2; 1024]);
        assert_eq!(&mmap[4096..8192], &[3; 4096]);
    }

    #[test]
    fn test_resize_shrink() {
        let file = tempfile::NamedTempFile::new().unwrap();
        let mut mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(8192).unwrap()).unwrap();
        assert_eq!(mmap.len(), 8192);
        assert_eq!(file.as_file().metadata().unwrap().len(), 8192);
        mmap[0..8192].copy_from_slice(&[1; 8192]);

        mmap.resize(NonZeroUsize::new(4096).unwrap()).unwrap();
        assert_eq!(mmap.len(), 4096);
        assert_eq!(file.as_file().metadata().unwrap().len(), 4096);
        assert_eq!(&mmap[0..4096], &[1; 4096]);
        mmap[2048..4096].copy_from_slice(&[2; 2048]);

        // munmap(2) succeeds.
        drop(mmap);

        let mmap = Mmap::new(file.reopen().unwrap(), NonZeroUsize::new(4096).unwrap()).unwrap();
        assert_eq!(mmap.len(), 4096);
        assert_eq!(file.as_file().metadata().unwrap().len(), 4096);
        assert_eq!(&mmap[0..2048], &[1; 2048]);
        assert_eq!(&mmap[2048..4096], &[2; 2048]);
    }
}
