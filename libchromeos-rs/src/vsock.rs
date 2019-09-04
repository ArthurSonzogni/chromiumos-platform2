// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Support for virtual sockets.
use std::fmt;
use std::io;
use std::mem::{self, size_of};
use std::os::raw::{c_int, c_uchar, c_uint, c_ushort};
use std::os::unix::io::{AsRawFd, IntoRawFd, RawFd};
use std::result;
use std::str::FromStr;

use libc::{self, c_void, sa_family_t, size_t, sockaddr, socklen_t, F_GETFL, F_SETFL, O_NONBLOCK};

// The domain for vsock sockets.
const AF_VSOCK: sa_family_t = 40;

// Vsock equivalent of INADDR_ANY.  Indicates the context id of the current endpoint.
pub const VMADDR_CID_ANY: c_uint = c_uint::max_value();

// Vsock equivalent of binding on port 0. Binds to a random port.
pub const VMADDR_PORT_ANY: c_uint = c_uint::max_value();

// The number of bytes of padding to be added to the sockaddr_vm struct.  Taken directly
// from linux/vm_sockets.h.
const PADDING: usize = size_of::<sockaddr>()
    - size_of::<sa_family_t>()
    - size_of::<c_ushort>()
    - (2 * size_of::<c_uint>());

#[repr(C)]
struct sockaddr_vm {
    svm_family: sa_family_t,
    svm_reserved1: c_ushort,
    svm_port: c_uint,
    svm_cid: c_uint,
    svm_zero: [c_uchar; PADDING],
}

pub struct AddrParseError;

impl fmt::Display for AddrParseError {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "failed to parse vsock address")
    }
}

/// An address associated with a virtual socket.
#[derive(Copy, Clone)]
pub struct SocketAddr {
    pub cid: c_uint,
    pub port: c_uint,
}

pub trait ToSocketAddr {
    fn to_socket_addr(&self) -> result::Result<SocketAddr, AddrParseError>;
}

impl ToSocketAddr for SocketAddr {
    fn to_socket_addr(&self) -> result::Result<SocketAddr, AddrParseError> {
        Ok(*self)
    }
}

impl ToSocketAddr for str {
    fn to_socket_addr(&self) -> result::Result<SocketAddr, AddrParseError> {
        self.parse()
    }
}

impl<'a, T: ToSocketAddr + ?Sized> ToSocketAddr for &'a T {
    fn to_socket_addr(&self) -> result::Result<SocketAddr, AddrParseError> {
        (**self).to_socket_addr()
    }
}

impl FromStr for SocketAddr {
    type Err = AddrParseError;

    /// Parse a vsock SocketAddr from a string. vsock socket addresses are of the form
    /// "vsock:cid:port".
    fn from_str(s: &str) -> Result<SocketAddr, AddrParseError> {
        let components: Vec<&str> = s.split(':').collect();
        if components.len() != 3 || components[0] != "vsock" {
            return Err(AddrParseError);
        }

        Ok(SocketAddr {
            cid: components[1].parse().map_err(|_| AddrParseError)?,
            port: components[2].parse().map_err(|_| AddrParseError)?,
        })
    }
}

impl fmt::Display for SocketAddr {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "{}:{}", self.cid, self.port)
    }
}

/// Sets `fd` to be blocking or nonblocking. `fd` must be a valid fd of a type that accepts the
/// `O_NONBLOCK` flag. This includes regular files, pipes, and sockets.
unsafe fn set_nonblocking(fd: RawFd, nonblocking: bool) -> io::Result<()> {
    let flags = libc::fcntl(fd, F_GETFL, 0);
    if flags < 0 {
        return Err(io::Error::last_os_error());
    }

    let flags = if nonblocking {
        flags | O_NONBLOCK
    } else {
        flags & !O_NONBLOCK
    };

    let ret = libc::fcntl(fd, F_SETFL, flags);
    if ret < 0 {
        return Err(io::Error::last_os_error());
    }

    Ok(())
}

/// A virtual stream socket.
pub struct VsockStream {
    fd: RawFd,
}

impl VsockStream {
    pub fn connect<A: ToSocketAddr>(addr: A) -> io::Result<VsockStream> {
        let sockaddr = addr
            .to_socket_addr()
            .map_err(|_| io::Error::from_raw_os_error(libc::EINVAL))?;

        // Safe because this just creates a vsock socket, and the return value is checked.
        let sockfd =
            unsafe { libc::socket(libc::AF_VSOCK, libc::SOCK_STREAM | libc::SOCK_CLOEXEC, 0) };
        if sockfd < 0 {
            return Err(io::Error::last_os_error());
        }

        // Safe because we are zero-initializing a struct with only integer fields.
        let mut svm: sockaddr_vm = unsafe { mem::zeroed() };
        svm.svm_family = AF_VSOCK;
        svm.svm_cid = sockaddr.cid;
        svm.svm_port = sockaddr.port;

        // Safe because this just connects a vsock socket, and the return value is checked.
        let ret = unsafe {
            libc::connect(
                sockfd,
                &svm as *const sockaddr_vm as *const sockaddr,
                size_of::<sockaddr_vm>() as socklen_t,
            )
        };
        if ret < 0 {
            let connect_err = io::Error::last_os_error();
            // Safe because this doesn't modify any memory and we are the only
            // owner of the file descriptor.
            unsafe { libc::close(sockfd) };
            return Err(connect_err);
        }

        Ok(VsockStream { fd: sockfd })
    }

    pub fn try_clone(&self) -> io::Result<VsockStream> {
        // Safe because this doesn't modify any memory and we check the return value.
        let dup_fd = unsafe { libc::fcntl(self.fd, libc::F_DUPFD_CLOEXEC, 0) };
        if dup_fd < 0 {
            return Err(io::Error::last_os_error());
        }

        Ok(VsockStream { fd: dup_fd })
    }

    pub fn set_nonblocking(&mut self, nonblocking: bool) -> io::Result<()> {
        // Safe because the fd is valid and owned by this stream.
        unsafe { set_nonblocking(self.fd, nonblocking) }
    }
}

impl io::Read for VsockStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        // Safe because this will only modify the contents of |buf| and we check the return value.
        let ret = unsafe {
            libc::read(
                self.fd,
                buf as *mut [u8] as *mut c_void,
                buf.len() as size_t,
            )
        };
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }

        Ok(ret as usize)
    }
}

impl io::Write for VsockStream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        // Safe because this doesn't modify any memory and we check the return value.
        let ret = unsafe {
            libc::write(
                self.fd,
                buf as *const [u8] as *const c_void,
                buf.len() as size_t,
            )
        };
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }

        Ok(ret as usize)
    }

    fn flush(&mut self) -> io::Result<()> {
        // No buffered data so nothing to do.
        Ok(())
    }
}

impl AsRawFd for VsockStream {
    fn as_raw_fd(&self) -> RawFd {
        self.fd
    }
}

impl IntoRawFd for VsockStream {
    fn into_raw_fd(self) -> RawFd {
        let fd = self.fd;
        mem::forget(self);
        fd
    }
}

impl Drop for VsockStream {
    fn drop(&mut self) {
        // Safe because this doesn't modify any memory and we are the only
        // owner of the file descriptor.
        unsafe { libc::close(self.fd) };
    }
}

/// Represents a virtual socket server.
pub struct VsockListener {
    fd: RawFd,
}

impl VsockListener {
    /// Creates a new `VsockListener` bound to the specified port on the current virtual socket
    /// endpoint.
    pub fn bind(port: c_uint) -> io::Result<VsockListener> {
        // The compiler should optimize this out since these are both compile-time constants.
        assert_eq!(size_of::<sockaddr_vm>(), size_of::<sockaddr>());

        // Safe because this doesn't modify any memory and we check the return value.
        let fd: RawFd = unsafe {
            libc::socket(
                c_int::from(AF_VSOCK),
                libc::SOCK_STREAM | libc::SOCK_CLOEXEC,
                0,
            )
        };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }

        // Safe because we are zero-initializing a struct with only integer fields.
        let mut svm: sockaddr_vm = unsafe { mem::zeroed() };
        svm.svm_family = AF_VSOCK;
        svm.svm_cid = VMADDR_CID_ANY;
        svm.svm_port = port;

        // Safe because this doesn't modify any memory and we check the return value.
        let ret = unsafe {
            libc::bind(
                fd,
                &svm as *const sockaddr_vm as *const sockaddr,
                size_of::<sockaddr_vm>() as socklen_t,
            )
        };
        if ret < 0 {
            let bind_err = io::Error::last_os_error();
            // Safe because this doesn't modify any memory and we are the only
            // owner of the file descriptor.
            unsafe { libc::close(fd) };
            return Err(bind_err);
        }

        // Safe because this doesn't modify any memory and we check the return value.
        let ret = unsafe { libc::listen(fd, 1) };
        if ret < 0 {
            let listen_err = io::Error::last_os_error();
            // Safe because this doesn't modify any memory and we are the only
            // owner of the file descriptor.
            unsafe { libc::close(fd) };
            return Err(listen_err);
        }

        Ok(VsockListener { fd })
    }

    /// Returns the port that this listener is bound to.
    pub fn local_port(&self) -> io::Result<u32> {
        // Safe because we are zero-initializing a struct with only integer fields.
        let mut svm: sockaddr_vm = unsafe { mem::zeroed() };

        // Safe because we give a valid pointer for addrlen and check the length.
        let mut addrlen = size_of::<sockaddr_vm>() as socklen_t;
        let ret = unsafe {
            // Get the socket address that was actually bound.
            libc::getsockname(
                self.fd,
                &mut svm as *mut sockaddr_vm as *mut sockaddr,
                &mut addrlen as *mut socklen_t,
            )
        };
        if ret < 0 {
            let getsockname_err = io::Error::last_os_error();
            return Err(getsockname_err);
        }
        // If this doesn't match, it's not safe to get the port out of the sockaddr.
        assert_eq!(addrlen as usize, size_of::<sockaddr_vm>());

        Ok(svm.svm_port)
    }

    /// Accepts a new incoming connection on this listener.  Blocks the calling thread until a
    /// new connection is established.  When established, returns the corresponding `VsockStream`
    /// and the remote peer's address.
    pub fn accept(&self) -> io::Result<(VsockStream, SocketAddr)> {
        // Safe because we are zero-initializing a struct with only integer fields.
        let mut svm: sockaddr_vm = unsafe { mem::zeroed() };

        // Safe because this will only modify |svm| and we check the return value.
        let mut socklen: socklen_t = size_of::<sockaddr_vm>() as socklen_t;
        let fd = unsafe {
            libc::accept4(
                self.fd,
                &mut svm as *mut sockaddr_vm as *mut sockaddr,
                &mut socklen as *mut socklen_t,
                libc::SOCK_CLOEXEC,
            )
        };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }

        if svm.svm_family != AF_VSOCK {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("unexpected address family: {}", svm.svm_family),
            ));
        }

        Ok((
            VsockStream { fd },
            SocketAddr {
                cid: svm.svm_cid,
                port: svm.svm_port,
            },
        ))
    }

    pub fn set_nonblocking(&mut self, nonblocking: bool) -> io::Result<()> {
        // Safe because the fd is valid and owned by this stream.
        unsafe { set_nonblocking(self.fd, nonblocking) }
    }
}

impl AsRawFd for VsockListener {
    fn as_raw_fd(&self) -> RawFd {
        self.fd
    }
}

impl Drop for VsockListener {
    fn drop(&mut self) {
        // Safe because this doesn't modify any memory and we are the only
        // owner of the file descriptor.
        unsafe { libc::close(self.fd) };
    }
}
