// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod arguments;
mod hotplug;
mod listeners;

use std::fmt;
use std::io;
use std::net::SocketAddr;
use std::net::TcpListener as StdTcpListener;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use dbus::blocking::Connection;
use ippusb::{device_supports_ippusb, Bridge, ShutdownReason};
use log::{error, info};
use rusb::UsbContext;
use tokio::net::{TcpListener, TcpStream, UnixListener, UnixStream};
use tokio::runtime::Builder;
use tokio::signal;
use tokio::signal::unix::{self, SignalKind};
use tokio::sync::mpsc;

use crate::arguments::Args;
use crate::hotplug::UnplugDetector;
use crate::listeners::ScopedUnixListener;

#[derive(Debug)]
pub enum Error {
    CreateSocket(io::Error),
    CreateUsbDevice(ippusb::Error),
    DBus(dbus::Error),
    ParseArgs(arguments::Error),
    Syslog(syslog::Error),
    TokioRuntime(io::Error),
    Forwarder(io::Error),
    CreateContext(rusb::Error),
    DeviceList(rusb::Error),
    NoDevice,
    OpenDevice(rusb::Error),
    RegisterCallback(rusb::Error),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            CreateSocket(err) => write!(f, "Failed to create socket: {}", err),
            CreateUsbDevice(err) => write!(f, "Failed to create IPP-USB device: {}", err),
            DBus(err) => write!(f, "DBus error: {}", err),
            ParseArgs(err) => write!(f, "Failed to parse arguments: {}", err),
            Syslog(err) => write!(f, "Failed to initalize syslog: {}", err),
            TokioRuntime(err) => write!(f, "Error setting up tokio runtime: {}", err),
            Forwarder(err) => write!(f, "Error during internal forwarding: {}", err),
            CreateContext(err) => write!(f, "Failed to create UsbContext: {}", err),
            DeviceList(err) => write!(f, "Failed to read device list: {}", err),
            NoDevice => write!(f, "No valid IPP USB device found."),
            OpenDevice(err) => write!(f, "Failed to open device: {}", err),
            RegisterCallback(err) => write!(f, "Failed to register for hotplug callback: {}", err),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

// Set to true if the program should terminate.
static SHUTDOWN: AtomicBool = AtomicBool::new(false);

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

fn open_device<T: UsbContext>(
    context: T,
    bus_device: Option<(u8, u8)>,
) -> Result<rusb::DeviceHandle<T>> {
    let device_list = rusb::DeviceList::new_with_context(context).map_err(Error::DeviceList)?;

    let device = match bus_device {
        Some((bus, address)) => device_list
            .iter()
            .find(|d| d.bus_number() == bus && d.address() == address),
        None => device_list
            .iter()
            .find(|d| device_supports_ippusb(d).unwrap_or(false)),
    }
    .ok_or(Error::NoDevice)?;

    info!(
        "Selected device {}:{}",
        device.bus_number(),
        device.address()
    );

    device.open().map_err(Error::OpenDevice)
}

fn run() -> Result<()> {
    let argv: Vec<String> = std::env::args().collect();
    let args = match Args::parse(&argv).map_err(Error::ParseArgs)? {
        None => return Ok(()),
        Some(args) => args,
    };
    if args.verbose_log {
        syslog::init_unix(syslog::Facility::LOG_USER, log::LevelFilter::Trace)
            .map_err(Error::Syslog)?;
    } else {
        syslog::init_unix(syslog::Facility::LOG_USER, log::LevelFilter::Debug)
            .map_err(Error::Syslog)?;
    }

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

    // Try to create ippusb::Device in loop until it is created successfully or
    // an unexpected error occurs. ReadConfigDescriptor error means that access
    // to USB is blocked by usbguard (e.g.: the screen is locked).
    let usb: ippusb::Device;
    let context = rusb::Context::new().map_err(Error::CreateContext)?;
    loop {
        let device = open_device(context.clone(), args.bus_device);
        match device {
            Ok(handle) => {
                match ippusb::Device::new(args.verbose_log, handle) {
                    Ok(obj) => {
                        usb = obj;
                        break;
                    }
                    Err(ippusb::Error::ReadConfigDescriptor(..)) => {}
                    Err(err) => return Err(Error::CreateUsbDevice(err)),
                };
            }
            Err(err) => {
                error!("Failed to open device: {}", err);
                return Err(err);
            }
        };
        info!("Failed to create USB connector. Waiting for the signal from DBus.");
        dbus_conn.process(Duration::MAX).map_err(Error::DBus)?;
    }
    info!("USB connector created successfully.");

    let (shutdown_tx, shutdown_rx) = mpsc::channel(1);
    let _unplug = if rusb::has_hotplug() {
        Some(UnplugDetector::new(
            usb.device(),
            shutdown_tx.clone(),
            &SHUTDOWN,
            args.upstart_mode,
        ))
    } else {
        None
    };

    // Respond to both SIGINT and SIGTERM by doing a clean shutdown.  Deliberately
    // use unwrap in these functions because if something goes wrong with signal handling
    // then we need the process to exit anyway.
    let runtime = Builder::new_multi_thread()
        .enable_all()
        .build()
        .map_err(Error::TokioRuntime)?;
    let signal_tx = shutdown_tx.clone();
    runtime.spawn(async move {
        signal::ctrl_c().await.unwrap();
        SHUTDOWN.store(true, Ordering::Relaxed);
        signal_tx.send(ShutdownReason::Signal).await.unwrap();
    });
    let signal_tx = shutdown_tx.clone();
    runtime.spawn(async move {
        unix::signal(SignalKind::terminate())
            .unwrap()
            .recv()
            .await
            .unwrap();
        SHUTDOWN.store(true, Ordering::Relaxed);
        signal_tx.send(ShutdownReason::Signal).await.unwrap();
    });

    // If a socket path was passed, also start a forwarder that will connect to the main TCP
    // listener.
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

    let handle = runtime.handle().clone();
    if let Err(err) = runtime.block_on(async move {
        listener
            .set_nonblocking(true)
            .map_err(Error::CreateSocket)?;
        let async_listener = TcpListener::from_std(listener).map_err(Error::CreateSocket)?;
        let mut daemon = Bridge::new(args.verbose_log, shutdown_rx, async_listener, usb, handle);
        daemon.run().await;
        Ok::<(), Error>(())
    }) {
        error!("Daemon failed to run: {}", err);
    }

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
