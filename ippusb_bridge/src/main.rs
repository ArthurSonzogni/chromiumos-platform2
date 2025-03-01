// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod arguments;
mod error;
mod hotplug;
mod http;
mod io_adapters;
mod listeners;
mod usb_connector;
mod util;

use std::fmt;
use std::io;
use std::net::SocketAddr;
use std::net::TcpListener as StdTcpListener;
use std::os::raw::c_int;
use std::os::unix::io::{AsRawFd, IntoRawFd, RawFd};
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::time::Duration;

use dbus::blocking::Connection;
use libchromeos::deprecated::{EventFd, PollContext, PollToken};
use libchromeos::signal::register_signal_handler;
use log::{debug, error, info};
use nix::sys::signal::Signal;
use tiny_http::{ClientConnection, Stream};
use tokio::net::{TcpStream, UnixListener, UnixStream};
use tokio::runtime::Builder;

use crate::arguments::Args;
use crate::error::Error::ReadConfigDescriptor;
use crate::hotplug::UnplugDetector;
use crate::http::handle_request;
use crate::listeners::ScopedUnixListener;
use crate::usb_connector::UsbConnector;

#[derive(Debug)]
pub enum Error {
    CreateSocket(io::Error),
    CreateUsbConnector(error::Error),
    DBus(dbus::Error),
    EventFd(io::Error),
    ParseArgs(arguments::Error),
    PollEvents(nix::Error),
    RegisterHandler(nix::Error),
    Syslog(syslog::Error),
    SysUtil(nix::Error),
    TokioRuntime(io::Error),
    Forwarder(io::Error),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            CreateSocket(err) => write!(f, "Failed to create socket: {}", err),
            CreateUsbConnector(err) => write!(f, "Failed to create USB connector: {}", err),
            DBus(err) => write!(f, "DBus error: {}", err),
            EventFd(err) => write!(f, "Failed to create/duplicate EventFd: {}", err),
            ParseArgs(err) => write!(f, "Failed to parse arguments: {}", err),
            PollEvents(err) => write!(f, "Failed to poll for events: {}", err),
            RegisterHandler(err) => write!(f, "Registering SIGINT handler failed: {}", err),
            Syslog(err) => write!(f, "Failed to initalize syslog: {}", err),
            SysUtil(err) => write!(f, "Sysutil error: {}", err),
            TokioRuntime(err) => write!(f, "Error setting up tokio runtime: {}", err),
            Forwarder(err) => write!(f, "Error during internal forwarding: {}", err),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

// Set to true if the program should terminate.
static SHUTDOWN: AtomicBool = AtomicBool::new(false);

// Holds a raw EventFD with 'static lifetime that can be used to wake up any
// polling threads.
static SHUTDOWN_FD: AtomicI32 = AtomicI32::new(-1);

extern "C" fn sigint_handler(_: c_int) {
    // Check if we've already received one SIGINT. If we have, the program may be misbehaving and
    // not terminating, so to be safe we'll forcefully exit.
    if SHUTDOWN.load(Ordering::Relaxed) {
        std::process::exit(1);
    }
    SHUTDOWN.store(true, Ordering::Relaxed);
    let fd = SHUTDOWN_FD.load(Ordering::Relaxed);
    if fd >= 0 {
        let buf = &1u64 as *const u64 as *const libc::c_void;
        let size = std::mem::size_of::<u64>();
        unsafe { libc::write(fd, buf, size) };
    }
}

/// Registers a SIGINT handler that, when triggered, will write to `shutdown_fd`
/// to notify any listeners of a pending shutdown.
fn add_sigint_handler(shutdown_fd: EventFd) -> nix::Result<()> {
    // Leak our copy of the fd to ensure SHUTDOWN_FD remains valid until ippusb_bridge closes, so
    // that we aren't inadvertently writing to an invalid FD in the SIGINT handler. The FD will be
    // reclaimed by the OS once our process has stopped.
    SHUTDOWN_FD.store(shutdown_fd.into_raw_fd(), Ordering::Relaxed);

    // Safe because sigint_handler is an extern "C" function that only performs
    // async signal-safe operations.
    unsafe { register_signal_handler(Signal::SIGINT, sigint_handler) }
}

struct Daemon {
    verbose_log: bool,
    num_clients: usize,

    shutdown: EventFd,
    listener: StdTcpListener,
    usb: UsbConnector,
}

// Trivially allows a `RawFd` to be passed as a `&AsRawFd`.  Needed because
// `Daemon` contains an `Accept` but needs to pass it to `PollContext` as a
// `&AsRawFd`.
struct WrapFd(RawFd);
impl AsRawFd for WrapFd {
    fn as_raw_fd(&self) -> RawFd {
        self.0
    }
}

impl Daemon {
    fn new(
        verbose_log: bool,
        shutdown: EventFd,
        listener: StdTcpListener,
        usb: UsbConnector,
    ) -> Result<Self> {
        Ok(Self {
            verbose_log,
            num_clients: 0,
            shutdown,
            listener,
            usb,
        })
    }

    fn run(&mut self) -> Result<()> {
        #[derive(PollToken)]
        enum Token {
            Shutdown,
            ClientConnection,
        }

        let listener_fd = WrapFd(self.listener.as_raw_fd());
        let poll_ctx: PollContext<Token> = PollContext::build_with(&[
            (&self.shutdown, Token::Shutdown),
            (&listener_fd, Token::ClientConnection),
        ])
        .map_err(Error::SysUtil)?;

        'poll: loop {
            let timeout = Duration::new(i64::MAX as u64, 0);
            let events = poll_ctx.wait_timeout(timeout).map_err(Error::PollEvents)?;
            for event in &events {
                match event.token() {
                    Token::Shutdown => break 'poll,
                    Token::ClientConnection => match self.listener.accept() {
                        Ok((stream, addr)) => {
                            info!("Connection opened from {}", addr);
                            self.handle_connection(stream.into())
                        }
                        Err(err) => error!("Failed to accept connection: {}", err),
                    },
                }
            }
        }
        Ok(())
    }

    fn handle_connection(&mut self, stream: Stream) {
        let connection = ClientConnection::new(stream);
        let mut thread_usb = self.usb.clone();
        let verbose = self.verbose_log;
        self.num_clients += 1;
        let client_num = self.num_clients;
        std::thread::spawn(move || {
            if verbose {
                debug!("Connection {} opened", client_num);
            }
            let mut num_requests = 0;
            for request in connection {
                num_requests += 1;
                let usb_conn = match thread_usb.get_connection() {
                    Ok(c) => c,
                    Err(e) => {
                        error!("Getting USB connection failed: {}", e);
                        continue;
                    }
                };

                if let Err(e) = handle_request(verbose, usb_conn, request) {
                    error!("Handling request failed: {}", e);
                }
            }
            if verbose {
                debug!(
                    "Connection {} handled {} requests",
                    client_num, num_requests
                );
            }
        });
    }
}

async fn forward_connection(
    conn_count: usize,
    mut incoming: UnixStream,
    local_addr: SocketAddr,
) -> Result<()> {
    let mut outbound = TcpStream::connect(local_addr)
        .await
        .map_err(Error::Forwarder)?;
    let (in_size, out_size) = tokio::io::copy_bidirectional(&mut incoming, &mut outbound)
        .await
        .map_err(Error::Forwarder)?;
    info!(
        "Forwarder on connection {} closed with {} byes sent, {} bytes returned",
        conn_count, in_size, out_size
    );
    Ok(())
}

fn run() -> Result<()> {
    syslog::init_unix(syslog::Facility::LOG_USER, log::LevelFilter::Debug)
        .map_err(Error::Syslog)?;
    let argv: Vec<String> = std::env::args().collect();
    let args = match Args::parse(&argv).map_err(Error::ParseArgs)? {
        None => return Ok(()),
        Some(args) => args,
    };

    let shutdown_fd = EventFd::new().map_err(|e| Error::EventFd(e.into()))?;
    let sigint_shutdown_fd = shutdown_fd.try_clone().map_err(Error::EventFd)?;
    add_sigint_handler(sigint_shutdown_fd).map_err(Error::RegisterHandler)?;

    // Safe because the syscall doesn't touch any memory and always succeeds.
    unsafe { libc::umask(0o117) };

    let host = format!("127.0.0.1:{}", args.tcp_port.unwrap_or(0));
    let listener = StdTcpListener::bind(host).map_err(Error::CreateSocket)?;
    let local_addr = listener.local_addr().map_err(Error::CreateSocket)?;
    info!("Listening on {}", local_addr);

    // Start up a connection to the system bus.
    // We need to listen to a signal sent when access to USB is restored. It
    // will be needed if access to USB is blocked by usbguard.
    let mut dbus_rule = dbus::message::MatchRule::new();
    dbus_rule.path = Some("/com/ubuntu/Upstart/jobs/usbguard_2don_2dunlock".into());
    dbus_rule.interface = Some("com.ubuntu.Upstart0_6.Job".into());
    dbus_rule.member = Some("InstanceRemoved".into());
    let dbus_conn = Connection::new_system().map_err(Error::DBus)?;
    dbus_conn
        .add_match(dbus_rule, |_: (), _, _msg: &dbus::Message| {
            info!("Received signal that access to USB was restored.");
            true
        })
        .map_err(Error::DBus)?;
    // This is run to make sure there are no cached signals from DBus.
    while dbus_conn.process(Duration::ZERO).map_err(Error::DBus)? {}

    // Try to create UsbConnector in loop until it is created successfully or
    // an unexpected error occurs. ReadConfigDescriptor error means that access
    // to USB is blocked by usbguard (e.g.: the screen is locked).
    let usb: UsbConnector;
    loop {
        match UsbConnector::new(args.verbose_log, args.bus_device) {
            Ok(obj) => {
                usb = obj;
                break;
            }
            Err(ReadConfigDescriptor(..)) => {}
            Err(err) => return Err(Error::CreateUsbConnector(err)),
        };
        info!("Failed to create USB connector. Waiting for the signal from DBus.");
        dbus_conn.process(Duration::MAX).map_err(Error::DBus)?;
    }
    info!("USB connector created successfully.");

    let _unplug = if rusb::has_hotplug() {
        let unplug_shutdown_fd = shutdown_fd.try_clone().map_err(Error::EventFd)?;
        Some(UnplugDetector::new(
            usb.device(),
            unplug_shutdown_fd,
            &SHUTDOWN,
            args.upstart_mode,
        ))
    } else {
        None
    };

    // If a socket path was passed, also start a forwarder that will connect to the main TCP
    // listener.
    let runtime = Builder::new_multi_thread()
        .enable_all()
        .build()
        .map_err(Error::TokioRuntime)?;
    if let Some(ref unix_socket_path) = args.unix_socket {
        info!("Forwarder listening on {}", unix_socket_path.display());
        let _guard = runtime.enter();
        let forwarder =
            ScopedUnixListener(UnixListener::bind(unix_socket_path).map_err(Error::CreateSocket)?);

        let handle = runtime.handle().clone();
        runtime.spawn(async move {
            let mut conn_count: usize = 1;
            loop {
                match forwarder.accept().await {
                    Ok((incoming, _)) => {
                        info!("New connection {} forwarding to {}", conn_count, local_addr);
                        handle.spawn(async move {
                            if let Err(err) =
                                forward_connection(conn_count, incoming, local_addr).await
                            {
                                error!("Forwarding error on connection {}: {}", conn_count, err);
                            }
                        });
                    }
                    Err(err) => {
                        error!(
                            "Forwarder failed to accept connection {}: {}",
                            conn_count, err
                        );
                    }
                }
                conn_count += 1;
            }
        });
    }

    let mut daemon = Daemon::new(args.verbose_log, shutdown_fd, listener, usb)?;
    daemon.run()?;

    info!("Shutting down.");
    runtime.shutdown_timeout(Duration::from_millis(500));
    Ok(())
}

fn main() {
    libchromeos::panic_handler::install_memfd_handler();
    // Use run() instead of returning a Result from main() so that we can print
    // errors using Display instead of Debug.
    if let Err(e) = run() {
        error!("{}", e);
    }
}
