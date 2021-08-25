// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The broker daemon that supports Trichecus from within the Chrome OS guest machine.

use std::cell::RefCell;
use std::env;
use std::fmt::{self, Debug, Formatter};
use std::io;
use std::os::unix::net::UnixDatagram;
use std::rc::Rc;
use std::time::Duration;

use dbus::arg::OwnedFd;
use dbus::blocking::LocalConnection;
use dbus_tree::{Interface, MTFn};
use getopts::Options;
use libsirenia::build_info::BUILD_TIMESTAMP;
use libsirenia::cli::trichechus::initialize_common_arguments;
use libsirenia::communication::trichechus::{AppInfo, Trichechus, TrichechusClient};
use libsirenia::rpc;
use libsirenia::transport::{
    self, Transport, TransportType, DEFAULT_CLIENT_PORT, DEFAULT_SERVER_PORT,
};
use sirenia::server::{org_chromium_mana_teeinterface_server, OrgChromiumManaTEEInterface};
use sys_util::{error, info, syslog};
use thiserror::Error as ThisError;

const GET_LOGS_SHORT_NAME: &str = "l";

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed to open D-Bus connection: {0}")]
    ConnectionRequest(dbus::Error),
    #[error("failed to register D-Bus handler: {0}")]
    DbusRegister(dbus::Error),
    #[error("failed to process the D-Bus message: {0}")]
    ProcessMessage(dbus::Error),
    #[error("failed to call rpc: {0}")]
    Rpc(rpc::Error),
    #[error("failed connect to /dev/log: {0}")]
    RawSyslogConnect(io::Error),
    #[error("failed write to /dev/log: {0}")]
    RawSyslogWrite(io::Error),
    #[error("failed to start up the syslog: {0}")]
    SysLog(sys_util::syslog::Error),
    #[error("failed to bind to socket: {0}")]
    TransportBind(transport::Error),
    #[error("failed to connect to socket: {0}")]
    TransportConnection(transport::Error),
    #[error("failed to get port: {0:}")]
    GetPort(transport::Error),
    #[error("failed to get client for transport: {0:}")]
    IntoClient(transport::Error),
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Copy, Clone, Default, Debug)]
struct TData;
impl dbus_tree::DataType for TData {
    type Tree = ();
    type ObjectPath = Rc<DugongDevice>;
    type Property = ();
    type Interface = ();
    type Method = ();
    type Signal = ();
}

struct DugongDevice {
    trichechus_client: RefCell<TrichechusClient>,
    transport_type: TransportType,
}

impl Debug for DugongDevice {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "transport_type: {:?}", self.transport_type)
    }
}

impl OrgChromiumManaTEEInterface for DugongDevice {
    fn start_teeapplication(
        &self,
        app_id: &str,
    ) -> std::result::Result<(i32, OwnedFd, OwnedFd), dbus_tree::MethodErr> {
        info!("Got request to start up: {}", app_id);
        let fds = request_start_tee_app(self, app_id);
        match fds {
            Ok(fds) => Ok((0, fds.0, fds.1)),
            Err(e) => Err(dbus_tree::MethodErr::failed(&e)),
        }
    }
}

fn request_start_tee_app(device: &DugongDevice, app_id: &str) -> Result<(OwnedFd, OwnedFd)> {
    let mut transport = device
        .transport_type
        .try_into_client(None)
        .map_err(Error::IntoClient)?;
    let addr = transport.bind().map_err(Error::TransportBind)?;
    let app_info = AppInfo {
        app_id: String::from(app_id),
        port_number: addr.get_port().map_err(Error::GetPort)?,
    };
    info!("Requesting start {:?}", &app_info);
    device
        .trichechus_client
        .borrow_mut()
        .start_session(app_info)
        .map_err(Error::Rpc)?;
    match transport.connect() {
        Ok(Transport { r, w, id: _ }) => unsafe {
            // This is safe because into_raw_fd transfers the ownership to OwnedFd.
            Ok((OwnedFd::new(r.into_raw_fd()), OwnedFd::new(w.into_raw_fd())))
        },
        Err(err) => Err(Error::TransportConnection(err)),
    }
}

fn handle_manatee_logs(dugong_device: &DugongDevice) -> Result<()> {
    const LOG_PATH: &str = "/dev/log";
    let trichechus_client = dugong_device.trichechus_client.borrow();
    let logs: Vec<Vec<u8>> = trichechus_client.get_logs().map_err(Error::Rpc)?;
    if logs.is_empty() {
        return Ok(());
    }

    // TODO(b/173600313) Decide whether to write this directly to a different log file.
    let raw_syslog = UnixDatagram::unbound().map_err(Error::RawSyslogConnect)?;
    for entry in logs.as_slice() {
        raw_syslog
            .send_to(entry, LOG_PATH)
            .map_err(Error::RawSyslogWrite)?;
    }

    Ok(())
}

pub fn start_dbus_handler(
    trichechus_client: TrichechusClient,
    transport_type: TransportType,
) -> Result<()> {
    let dugong_device = Rc::new(DugongDevice {
        trichechus_client: RefCell::new(trichechus_client),
        transport_type,
    });

    let c = LocalConnection::new_system().map_err(Error::ConnectionRequest)?;
    c.request_name(
        "org.chromium.ManaTEE",
        false, /*allow_replacement*/
        false, /*replace_existing*/
        false, /*do_not_queue*/
    )
    .map_err(Error::ConnectionRequest)?;
    let f = dbus_tree::Factory::new_fn();
    let interface: Interface<MTFn<TData>, TData> =
        org_chromium_mana_teeinterface_server(&f, (), |m| {
            let a: &Rc<DugongDevice> = m.path.get_data();
            let b: &DugongDevice = a;
            b
        });

    let tree = f.tree(()).add(
        f.object_path("/org/chromium/ManaTEE1", dugong_device.clone())
            .introspectable()
            .add(interface),
    );

    tree.start_receive(&c);
    info!("Finished dbus setup, starting handler.");
    loop {
        if let Err(err) = handle_manatee_logs(&dugong_device) {
            if matches!(err, Error::Rpc(_)) {
                error!("Trichechus disconnected: {}", err);
                return Err(err);
            }
            error!("Failed to forward syslog: {}", err);
        }
        c.process(Duration::from_millis(1000))
            .map_err(Error::ProcessMessage)?;
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let mut opts = Options::new();
    opts.optflag(
        GET_LOGS_SHORT_NAME,
        "get-logs",
        "connect to trichechus, get and print logs, then exit.",
    );
    let (config, matches) = initialize_common_arguments(opts, &args[1..]).unwrap();
    let get_logs = matches.opt_present(GET_LOGS_SHORT_NAME);
    let transport_type = config.connection_type.clone();

    if let Err(e) = syslog::init() {
        eprintln!("failed to initialize syslog: {}", e);
        return Err(e).map_err(Error::SysLog);
    }

    info!("Starting dugong: {}", BUILD_TIMESTAMP);
    info!("Opening connection to trichechus");
    // Adjust the source port when connecting to a non-standard port to facilitate testing.
    let bind_port = match transport_type.get_port().map_err(Error::GetPort)? {
        DEFAULT_SERVER_PORT => DEFAULT_CLIENT_PORT,
        port => port + 1,
    };
    let mut transport = transport_type
        .try_into_client(Some(bind_port))
        .map_err(Error::IntoClient)?;

    let transport = transport.connect().map_err(|e| {
        error!("transport connect failed: {}", e);
        Error::TransportConnection(e)
    })?;
    info!("Starting rpc");
    let client = TrichechusClient::new(transport);
    if get_logs {
        info!("Getting logs");
        let logs = client.get_logs().unwrap();
        for entry in &logs[..] {
            print!("{}", String::from_utf8_lossy(entry));
        }
    } else {
        start_dbus_handler(client, config.connection_type).unwrap();
        unreachable!()
    }
    Ok(())
}
