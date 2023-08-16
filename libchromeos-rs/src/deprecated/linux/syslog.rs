// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of the Syslog trait for Linux.

use std::{
    fmt,
    fs::File,
    io::{Cursor, ErrorKind, Write},
    mem,
    os::unix::{
        io::{AsRawFd, FromRawFd, RawFd},
        net::UnixDatagram,
    },
    ptr::null,
};

use libc::socketpair;
use libc::AF_UNIX;
use libc::FIOCLEX;
use libc::SOCK_SEQPACKET;
use libc::{
    closelog, fcntl, localtime_r, openlog, time, time_t, tm, F_GETFD, LOG_NDELAY, LOG_PERROR,
    LOG_PID, LOG_USER,
};

use crate::deprecated::syslog::{Error, Facility, Priority, Syslog};
use nix::unistd::getpid;

const SYSLOG_PATH: &str = "/dev/log";

pub struct PlatformSyslog {
    socket: Option<UnixDatagram>,
    test_socket: Option<UnixDatagram>,
}

impl Syslog for PlatformSyslog {
    fn new() -> Result<Self, Error> {
        let (socket, test_socket) = openlog_and_get_socket()?;
        Ok(Self {
            socket: Some(socket),
            test_socket,
        })
    }

    fn enable(&mut self, enable: bool) -> Result<(), Error> {
        match self.socket.take() {
            Some(_) if enable => {}
            Some(s) => {
                // Because `openlog_and_get_socket` actually just "borrows" the syslog FD, this
                // module does not own the syslog connection and therefore should not destroy it.
                mem::forget(s);
            }
            None if enable => {
                let s = openlog_and_get_socket()?;
                self.socket = Some(s.0);
                self.test_socket = s.1;
            }
            _ => {}
        }
        Ok(())
    }

    fn push_fds(&self, fds: &mut Vec<RawFd>) {
        fds.extend(self.socket.iter().map(|s| s.as_raw_fd()));
    }

    fn log(
        &self,
        proc_name: Option<&str>,
        pri: Priority,
        fac: Facility,
        file_line: Option<(&str, u32)>,
        args: fmt::Arguments,
    ) {
        const MONTHS: [&str; 12] = [
            "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
        ];

        let mut buf = [0u8; 1024];
        if let Some(socket) = &self.socket {
            let tm = get_localtime();
            let prifac = (pri as u8) | (fac as u8);
            let res = {
                let mut buf_cursor = Cursor::new(&mut buf[..]);
                write!(
                    &mut buf_cursor,
                    "<{}>{} {:02} {:02}:{:02}:{:02} {}[{}]: ",
                    prifac,
                    MONTHS[tm.tm_mon as usize],
                    tm.tm_mday,
                    tm.tm_hour,
                    tm.tm_min,
                    tm.tm_sec,
                    proc_name.unwrap_or("-"),
                    getpid()
                )
                .and_then(|()| {
                    if let Some((file_name, line)) = &file_line {
                        write!(&mut buf_cursor, " [{}:{}] ", file_name, line)
                    } else {
                        Ok(())
                    }
                })
                .and_then(|()| write!(&mut buf_cursor, "{}", args))
                .map(|()| buf_cursor.position() as usize)
            };

            if let Ok(len) = &res {
                send_buf(socket, &buf[..*len])
            }
        }
    }
}

// Uses libc's openlog function to get a socket to the syslogger. By getting the socket this way, as
// opposed to connecting to the syslogger directly, libc's internal state gets initialized for other
// libraries (e.g. minijail) that make use of libc's syslog function. Note that this function
// depends on no other threads or signal handlers being active in this process because they might
// create FDs.
//
// TODO(zachr): Once https://android-review.googlesource.com/470998 lands, there won't be any
// libraries in use that hard depend on libc's syslogger. Remove this and go back to making the
// connection directly once minjail is ready.
pub fn openlog_and_get_socket() -> Result<(UnixDatagram, Option<UnixDatagram>), Error> {
    // When running unit tests, do not use the real syslog.
    if cfg!(test) {
        return match new_seqpacket_pair() {
            Ok(a) => Ok((a.0, Some(a.1))),
            Err(err) => Err(Error::Socket(err.into())),
        };
    }

    // closelog first in case there was already a file descriptor open.  Safe because it takes no
    // arguments and just closes an open file descriptor.  Does nothing if the file descriptor
    // was not already open.
    unsafe {
        closelog();
    }

    // Ordinarily libc's FD for the syslog connection can't be accessed, but we can guess that the
    // FD that openlog will be getting is the lowest unused FD. To guarantee that an FD is opened in
    // this function we use the LOG_NDELAY to tell openlog to connect to the syslog now. To get the
    // lowest unused FD, we open a dummy file (which the manual says will always return the lowest
    // fd), and then close that fd. Voilà, we now know the lowest numbered FD. The call to openlog
    // will make use of that FD, and then we just wrap a `UnixDatagram` around it for ease of use.
    let fd = File::open("/dev/null")
        .map_err(Error::GetLowestFd)?
        .as_raw_fd();

    unsafe {
        // Safe because openlog accesses no pointers because `ident` is null, only valid flags are
        // used, and it returns no error.
        openlog(null(), LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);
        // For safety, ensure the fd we guessed is valid. The `fcntl` call itself only reads the
        // file descriptor table of the current process, which is trivially safe.
        if fcntl(fd, F_GETFD) >= 0 {
            Ok((UnixDatagram::from_raw_fd(fd), None))
        } else {
            Err(Error::InvalidFd)
        }
    }
}

pub fn new_seqpacket_pair() -> nix::Result<(UnixDatagram, UnixDatagram)> {
    let mut fds = [0, 0];
    // Safe because fds is owned and the return value is checked.
    let ret = unsafe { socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds.as_mut_ptr()) };
    if ret != 0 {
        return Err(nix::Error::last());
    }

    // Safe because the file descriptors aren't owned yet.
    let first = unsafe { UnixDatagram::from_raw_fd(fds[0]) };
    let second = unsafe { UnixDatagram::from_raw_fd(fds[1]) };

    // Set FD_CLOEXEC. Safe because this will not fail since the fds are valid.
    unsafe {
        libc::ioctl(first.as_raw_fd(), FIOCLEX, 0);
        libc::ioctl(second.as_raw_fd(), FIOCLEX, 0);
    }

    Ok((first, second))
}

/// Should only be called after `init()` was called.
fn send_buf(socket: &UnixDatagram, buf: &[u8]) {
    const SEND_RETRY: usize = 2;

    for _ in 0..SEND_RETRY {
        match socket.send(buf) {
            Ok(_) => break,
            Err(e) => match e.kind() {
                ErrorKind::ConnectionRefused
                | ErrorKind::ConnectionReset
                | ErrorKind::ConnectionAborted
                | ErrorKind::NotConnected => {
                    let res = socket.connect(SYSLOG_PATH);
                    if res.is_err() {
                        break;
                    }
                }
                _ => {}
            },
        }
    }
}

fn get_localtime() -> tm {
    unsafe {
        // Safe because tm is just a struct of plain data.
        let mut tm: tm = mem::zeroed();
        let mut now: time_t = 0;
        // Safe because we give time a valid pointer and can never fail.
        time(&mut now as *mut _);
        // Safe because we give localtime_r valid pointers and can never fail.
        localtime_r(&now, &mut tm as *mut _);
        tm
    }
}
