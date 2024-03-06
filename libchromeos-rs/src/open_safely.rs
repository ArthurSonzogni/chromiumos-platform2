// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust port of libbrillo's `OpenSafely` API.

use std::ffi::{CString, OsStr};
use std::fs::File;
use std::io;
use std::mem::MaybeUninit;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::{Component, Path};

use libc::{
    c_int, fcntl, mode_t, stat64, F_GETFL, F_SETFL, O_APPEND, O_CLOEXEC, O_CREAT, O_DIRECTORY,
    O_EXCL, O_NOFOLLOW, O_NONBLOCK, O_PATH, O_RDONLY, O_RDWR, O_TRUNC, O_WRONLY, S_IFDIR, S_IFIFO,
    S_IFMT, S_IFREG,
};
use thiserror::Error;

use crate::handle_eintr_errno;

#[derive(Error, Debug)]
pub enum OpenSafelyError {
    #[error("open_safely() requires an absolute path")]
    AbsolutePathRequired,
    #[error("got an empty path")]
    EmptyPath,
    #[error("expected a directory but got type {0:#}")]
    ExpectedDirectory(mode_t),
    #[error("expected a fifo but got type {0:#}")]
    ExpectedFifo(mode_t),
    #[error("expected a regular file but got type {0:#}")]
    ExpectedRegularFile(mode_t),
    #[error("fstat() failed: {0}")]
    Fstat(io::Error),
    #[error("fcntl(F_GETFL) failed: {0}")]
    GetFlags(io::Error),
    #[error("invalid path component")]
    InvalidPathComponent,
    #[error("open failed: {0}")]
    Open(io::Error),
    #[error("parent fd required for relative path")]
    ParentFdRequired,
    #[error("fcntl(F_SETFL) failed: {0}")]
    SetFlags(io::Error),
}

impl From<OpenSafelyError> for io::Error {
    fn from(value: OpenSafelyError) -> Self {
        match value {
            OpenSafelyError::Open(io_error) => io_error,
            _ => io::Error::new(io::ErrorKind::Other, "open_safely failed"),
        }
    }
}

pub type OpenSafelyResult<T> = std::result::Result<T, OpenSafelyError>;

/// Drop-in replacement for `std::fs::OpenOptions` that provides `open_safely()`.
#[derive(Clone, Debug)]
pub struct OpenSafelyOptions {
    read: bool,
    write: bool,
    append: bool,
    truncate: bool,
    create: bool,
    create_new: bool,
    mode: mode_t,
    custom_flags: c_int,
}

impl OpenSafelyOptions {
    pub fn new() -> Self {
        OpenSafelyOptions {
            read: false,
            write: false,
            append: false,
            truncate: false,
            create: false,
            create_new: false,
            mode: 0o666, // rw-rw-rw-
            custom_flags: 0,
        }
    }

    pub fn read(&mut self, read: bool) -> &mut Self {
        self.read = read;
        self
    }

    pub fn write(&mut self, write: bool) -> &mut Self {
        self.write = write;
        self
    }

    pub fn append(&mut self, append: bool) -> &mut Self {
        self.append = append;
        self
    }

    pub fn truncate(&mut self, truncate: bool) -> &mut Self {
        self.truncate = truncate;
        self
    }

    pub fn create(&mut self, create: bool) -> &mut Self {
        self.create = create;
        self
    }

    pub fn create_new(&mut self, create_new: bool) -> &mut Self {
        self.create_new = create_new;
        self
    }

    pub fn mode(&mut self, mode: mode_t) -> &mut Self {
        self.mode = mode;
        self
    }

    pub fn custom_flags(&mut self, custom_flags: c_int) -> &mut Self {
        self.custom_flags = custom_flags;
        self
    }

    fn flags(&self) -> c_int {
        let mut flags = 0;

        if self.create {
            flags |= O_CREAT;
        }
        if self.truncate {
            flags |= O_TRUNC;
        }
        if self.append {
            flags |= O_APPEND;
        }

        if self.create_new {
            // NOTE: if create_new is specified, create, truncate, and append are ignored.
            flags = O_CREAT | O_EXCL;
        }

        if self.read && self.write {
            flags |= O_RDWR;
        } else if self.write {
            flags |= O_WRONLY;
        } else if self.read {
            flags |= O_RDONLY;
        }

        flags
    }

    pub fn open_safely(&self, path: impl AsRef<Path>) -> OpenSafelyResult<OwnedFd> {
        open_safely(path, self.flags(), self.mode)
    }

    pub fn open(&self, path: impl AsRef<Path>) -> io::Result<File> {
        let fd = self.open_safely(path)?;
        Ok(File::from(fd))
    }

    pub fn open_at_safely(
        &self,
        parent_fd: BorrowedFd<'_>,
        path: impl AsRef<Path>,
    ) -> OpenSafelyResult<OwnedFd> {
        open_at_safely(parent_fd, path, self.flags(), self.mode)
    }

    pub fn open_at(&self, file: &File, path: impl AsRef<Path>) -> io::Result<File> {
        let fd = self.open_at_safely(file.as_fd(), path)?;
        Ok(File::from(fd))
    }

    pub fn open_fifo_safely(&self, path: impl AsRef<Path>) -> OpenSafelyResult<OwnedFd> {
        open_fifo_safely(path, self.flags(), self.mode)
    }
}

impl Default for OpenSafelyOptions {
    fn default() -> Self {
        Self::new()
    }
}

enum MaybeBorrowedFd<'fd> {
    Borrowed(BorrowedFd<'fd>),
    Owned(OwnedFd),
}

impl AsFd for MaybeBorrowedFd<'_> {
    fn as_fd(&self) -> BorrowedFd<'_> {
        match self {
            MaybeBorrowedFd::Borrowed(fd) => fd.as_fd(),
            MaybeBorrowedFd::Owned(fd) => fd.as_fd(),
        }
    }
}

// Safe wrapper for `openat()`.
fn openat(
    dirfd: Option<BorrowedFd>,
    pathname: &OsStr,
    flags: c_int,
    mode: mode_t,
) -> io::Result<OwnedFd> {
    let name_cstr = CString::new(pathname.as_bytes())?;

    // SAFETY: We pass a valid C string pointer and borrowed file descriptor.
    let fd = handle_eintr_errno!(unsafe {
        libc::openat64(
            dirfd.as_ref().map(BorrowedFd::as_raw_fd).unwrap_or(-1),
            name_cstr.as_ptr(),
            flags,
            mode,
        )
    });
    if fd < 0 {
        Err(io::Error::last_os_error())
    } else {
        // SAFETY: We have ownership of the raw file descriptor returned by `openat`.
        Ok(unsafe { OwnedFd::from_raw_fd(fd) })
    }
}

// Safe wrapper for `fstat()`.
fn fstat(fd: BorrowedFd) -> io::Result<stat64> {
    let mut st = MaybeUninit::<stat64>::zeroed();

    // SAFETY: We pass a valid `struct stat` buffer for `fstat()`
    if unsafe { libc::fstat64(fd.as_raw_fd(), st.as_mut_ptr()) } < 0 {
        return Err(io::Error::last_os_error());
    }

    // SAFETY: We check that the `stat()` function succeeded and filled out `st` above.
    Ok(unsafe { st.assume_init() })
}

fn open_safely_internal(
    parent_fd: Option<BorrowedFd>,
    path: &Path,
    flags: c_int,
    mode: mode_t,
) -> OpenSafelyResult<OwnedFd> {
    let final_component = path.file_name().ok_or(OpenSafelyError::EmptyPath)?;
    let parent_path = path.parent().ok_or(OpenSafelyError::EmptyPath)?;
    let mut components = parent_path.components().peekable();

    let parent_flags = O_NONBLOCK | O_RDONLY | O_DIRECTORY | O_PATH | O_NOFOLLOW | O_CLOEXEC;

    let mut parent_fd = if components.next_if(|c| c == &Component::RootDir).is_some() {
        // Absolute path - open the root directory.
        MaybeBorrowedFd::Owned(
            openat(None, OsStr::new("/"), parent_flags, 0).map_err(OpenSafelyError::Open)?,
        )
    } else {
        // Relative paths may begin with `CurDir` ("./a/b") or just a normal component ("a/b").
        let _cur_dir = components.next_if(|c| c == &Component::CurDir);

        // Relative path - use provided `parent_fd`.
        MaybeBorrowedFd::Borrowed(parent_fd.ok_or(OpenSafelyError::ParentFdRequired)?)
    };

    for component in components {
        let name = match component {
            Component::Normal(component) => component,
            Component::ParentDir => OsStr::new(".."),
            // "/" can only occur as the first component.
            // `Path::components()` normalizes "." away aside from the first component.
            // Prefix is only used on Windows.
            Component::RootDir | Component::CurDir | Component::Prefix(..) => {
                return Err(OpenSafelyError::InvalidPathComponent)
            }
        };
        parent_fd = MaybeBorrowedFd::Owned(
            openat(Some(parent_fd.as_fd()), name, parent_flags, 0)
                .map_err(OpenSafelyError::Open)?,
        );
    }

    // O_NONBLOCK is used to avoid hanging on edge cases (e.g. a serial port with flow control, or a
    // FIFO without a writer).
    let fd = openat(
        Some(parent_fd.as_fd()),
        final_component,
        flags | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC,
        mode,
    )
    .map_err(OpenSafelyError::Open)?;

    // Remove the O_NONBLOCK flag unless the original `flags` have it.
    if (flags & O_NONBLOCK) == 0 {
        // SAFETY: We pass a valid file descriptor.
        let flags = unsafe { fcntl(fd.as_raw_fd(), F_GETFL) };
        if flags == -1 {
            return Err(OpenSafelyError::GetFlags(io::Error::last_os_error()));
        }
        // SAFETY: We pass a valid file descriptor and flags.
        if unsafe { fcntl(fd.as_raw_fd(), F_SETFL, flags & !O_NONBLOCK) } != 0 {
            return Err(OpenSafelyError::SetFlags(io::Error::last_os_error()));
        }
    }

    Ok(fd)
}

/// Opens the absolute `path` to a regular file or directory ensuring that none of the path
/// components are symbolic links and returns a FD.
///
/// If `path` is relative, or contains any symbolic links, or points to a non-regular file or
/// directory, an error is returned instead. `mode` is ignored unless `flags` has either `O_CREAT`
/// or `O_TMPFILE`. Note that `O_CLOEXEC` is set so the file descriptor will not be inherited across
/// exec calls.
///
/// The opened FD is verified to be the correct type of file: if `flags` has `O_DIRECTORY`, the path
/// is expected to end in a directory; otherwise, the path must be to a regular file. (C++ libbrillo
/// porting note: this behavior differs from `OpenSafely()`, which accepts a directory even when
/// `O_DIRECTORY` was not specified in `flags`).
///
/// # Parameters
///
/// - `path` - An absolute path of the file to open.
/// - `flags` - Flags to pass to open.
/// - `mode - Mode to pass to open.
pub fn open_safely(
    path: impl AsRef<Path>,
    flags: c_int,
    mode: mode_t,
) -> OpenSafelyResult<OwnedFd> {
    let path = path.as_ref();

    if !path.is_absolute() {
        return Err(OpenSafelyError::AbsolutePathRequired);
    }

    let fd = open_safely_internal(None, path, flags, mode)?;

    // Ensure the opened file is a regular file or directory.
    let st = fstat(fd.as_fd()).map_err(OpenSafelyError::Fstat)?;
    let fd_type = st.st_mode & S_IFMT;

    // This detects a FIFO opened for reading, for example.
    match flags & O_DIRECTORY {
        O_DIRECTORY => {
            if fd_type != S_IFDIR {
                return Err(OpenSafelyError::ExpectedDirectory(fd_type));
            }
        }
        _ => {
            if fd_type != S_IFREG {
                return Err(OpenSafelyError::ExpectedRegularFile(fd_type));
            }
        }
    }

    Ok(fd)
}

/// Opens the `path` relative to the `parent_fd` to a regular file or directory ensuring that none
/// of the path components are symbolic links and returns a FD.
///
/// If `path` contains any symbolic links, or points to a non-regular file or directory, an error is
/// returned instead. `mode` is ignored unless `flags` has either `O_CREAT` or `O_TMPFILE`. Note
/// that `O_CLOEXEC` is set so the file descriptor will not be inherited across exec calls.
///
/// # Parameters
///
/// - `parent_fd` - The file descriptor of the parent directory
/// - `path` - An absolute path of the file to open
/// - `flags` - Flags to pass to open.
/// - `mode` - Mode to pass to open.
pub fn open_at_safely(
    parent_fd: BorrowedFd<'_>,
    path: impl AsRef<Path>,
    flags: c_int,
    mode: mode_t,
) -> OpenSafelyResult<OwnedFd> {
    let path = path.as_ref();
    let fd = open_safely_internal(Some(parent_fd), path, flags, mode)?;

    // Ensure the opened file is a regular file or directory.
    let st = fstat(fd.as_fd()).map_err(OpenSafelyError::Fstat)?;
    let fd_type = st.st_mode & S_IFMT;

    // This detects a FIFO opened for reading, for example.
    match flags & O_DIRECTORY {
        O_DIRECTORY => {
            if fd_type != S_IFDIR {
                return Err(OpenSafelyError::ExpectedDirectory(fd_type));
            }
        }
        _ => {
            if fd_type != S_IFREG {
                return Err(OpenSafelyError::ExpectedRegularFile(fd_type));
            }
        }
    }

    Ok(fd)
}

/// Opens the absolute `path` to a FIFO ensuring that none of the path components
/// are symbolic links and returns a FD.
///
/// If `path` is relative, or contains any symbolic links, or points to a non-regular file or
/// directory, an error is returned instead. `mode` is ignored unless `flags` has either
/// `O_CREAT` or `O_TMPFILE`.
///
/// # Parameters
///
/// - `path` - An absolute path of the file to open
/// - `flags` - Flags to pass to open.
/// - `mode` - Mode to pass to open.
pub fn open_fifo_safely(
    path: impl AsRef<Path>,
    flags: c_int,
    mode: mode_t,
) -> OpenSafelyResult<OwnedFd> {
    let path = path.as_ref();

    if !path.is_absolute() {
        return Err(OpenSafelyError::AbsolutePathRequired);
    }

    let fd = open_safely_internal(None, path, flags, mode)?;

    // Ensure the opened file is a FIFO.
    let st = fstat(fd.as_fd()).map_err(OpenSafelyError::Fstat)?;
    let fd_type = st.st_mode & S_IFMT;
    if fd_type == S_IFIFO {
        Ok(fd)
    } else {
        Err(OpenSafelyError::ExpectedFifo(fd_type))
    }
}
