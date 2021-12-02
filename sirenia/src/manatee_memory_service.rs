// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The hypervisor memory service for manaTEE.

#![deny(unsafe_op_in_unsafe_fn)]

use std::cell::RefCell;
use std::collections::HashMap;
use std::env;
use std::fmt::{Debug, Formatter};
use std::io::{ErrorKind, Read, Write};
use std::mem;
use std::os::unix::io::{AsRawFd, RawFd};
use std::path::PathBuf;
use std::rc::Rc;
use std::time::Duration;
use std::result::Result as StdResult;

use sys_util::{
    vsock::{SocketAddr as VSocketAddr, VsockCid},
    {error, handle_eintr_errno, info, syslog, warn},
};
use data_model::DataInit;
use anyhow::{anyhow, Context, Result};
use libsirenia::{
    build_info::BUILD_TIMESTAMP,
    linux::events::{
        AddEventSourceMutator, EventMultiplexer, EventSource, Mutator, RemoveFdMutator,
    },
    rpc::{ConnectionHandler, TransportServer},
    sys,
    transport::{
        Error as TransportError, Transport, TransportType, UnixServerTransport, DEFAULT_MMS_PORT,
    },
};

const CROS_GUEST_ID: u32 = 0;

#[repr(C)]
#[derive(Copy, Clone, Default)]
struct MmsMessageHeader {
    len: u32,
    msg_type: u32,
}
// Safe because MmsMessageHeader only contains plain data.
unsafe impl DataInit for MmsMessageHeader {}

fn read_obj<T: DataInit>(connection: &mut Transport) -> Result<T> {
    let mut bytes = vec![0; mem::size_of::<T>()];
    connection
        .r
        .read_exact(&mut bytes)
        .context("failed to read bytes")?;
    T::from_slice(&bytes)
        .context("failed to parse bytes")
        .map(|o| *o)
}

fn get_control_server_path(id: u32) -> PathBuf {
    PathBuf::from(format!("/run/mms_control_{}.sock", id))
}

fn wait_for_hangup(conn: &Transport) {
    let mut fds = libc::pollfd {
        fd: conn.r.as_raw_fd(),
        events: libc::POLLHUP,
        revents: 0,
    };
    // Safe because we give a valid pointer to a list (of 1) FD and check the
    // return value.
    let mut ret = unsafe { handle_eintr_errno!(libc::poll(&mut fds, 1, 10 * 1000)) };
    if ret == 0 {
        if fds.revents == libc::POLLHUP {
            return;
        }
        warn!("Long wait for client hangup");
        // Safe because we give a valid pointer to a list (of 1) FD and check the
        // return value.
        ret = unsafe { handle_eintr_errno!(libc::poll(&mut fds, 1, -1)) };
    }

    if ret == -1 || (fds.revents & libc::POLLHUP) == 0 {
        error!("Error cleaning up stale clients {} {}", sys::errno(), fds.revents);
    }
}

fn cleanup_control_server(id: u32, server: UnixServerTransport) {
    // Unlink the file to stop any new clients.
    if let Err(e) = std::fs::remove_file(get_control_server_path(id)) {
        warn!("Error unlinking control server {}: {:?}", id, e);
    }
    // Check if there is a pending client, and wait for the client to close if there is.
    match server.accept_with_timeout(Duration::ZERO) {
        Ok(conn) => {
            wait_for_hangup(&conn);
        }
        Err(e) => {
            if let TransportError::Accept(e) = e {
                if e.kind() != ErrorKind::TimedOut {
                    warn!("Error checking for trailing clients {}: {:?}", id, e);
                }
            }
        }
    }
}

#[derive(Debug)]
struct CrosVmClient {
    client: Transport,
    mem_size: u64,
    balloon_size: u64,
}

struct MmsState {
    cros_ctrl_connected: bool,
    clients: HashMap<u32, CrosVmClient>,
}

struct CtrlHandler {
    connection: Transport,
    state: Rc<RefCell<MmsState>>,
}

impl CtrlHandler {
    fn new(connection: Transport, state: Rc<RefCell<MmsState>>) -> Self {
        CtrlHandler { connection, state }
    }

    fn handle_message(&mut self) -> Result<()> {
        let header =
            read_obj::<MmsMessageHeader>(&mut self.connection).context("failed to read header")?;

        let mut bytes = vec![0; header.len as usize];
        self.connection
            .r
            .read_exact(&mut bytes)
            .with_context(|| "failed to read ctl message")?;

        let msg: Vec<u8> = match header.msg_type {
            _ => Err(anyhow!("unknown message type {}", header.msg_type)),
        }?;

        let mut resp_bytes = Vec::new();
        let resp_header = MmsMessageHeader {
            len: msg.len() as u32,
            msg_type: header.msg_type,
        };
        resp_bytes.extend_from_slice(resp_header.as_slice());
        resp_bytes.extend_from_slice(&msg);
        self.connection
            .w
            .write_all(&resp_bytes)
            .with_context(|| "failed writing response")?;
        Ok(())
    }
}

impl Debug for CtrlHandler {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("CtrlHandler").finish()
    }
}

impl EventSource for CtrlHandler {
    fn on_event(&mut self) -> StdResult<Option<Box<dyn Mutator>>, String> {
        if let Err(msg) = self.handle_message() {
            error!("Error processing message {}", msg);
        };
        Ok(None)
    }

    fn on_hangup(&mut self) -> std::result::Result<Option<Box<dyn Mutator>>, String> {
        let mut state = self.state.borrow_mut();
        state.cros_ctrl_connected = false;
        Ok(Some(Box::new(RemoveFdMutator(self.connection.as_raw_fd()))))
    }
}

impl AsRawFd for CtrlHandler {
    fn as_raw_fd(&self) -> RawFd {
        self.connection.as_raw_fd()
    }
}

struct CtrlServerHandler {
    state: Rc<RefCell<MmsState>>,
}

impl CtrlServerHandler {
    fn new(state: Rc<RefCell<MmsState>>) -> Self {
        CtrlServerHandler { state }
    }
}

impl ConnectionHandler for CtrlServerHandler {
    fn handle_incoming_connection(&mut self, connection: Transport) -> Option<Box<dyn Mutator>> {
        let mut state = self.state.borrow_mut();
        if state.cros_ctrl_connected {
            error!("Duplicate control connection {}", connection.id);
            return None;
        }
        if state.clients.len() != 1 {
            error!("unknown crosvm clients");
            return None;
        }
        let handler = CtrlHandler::new(connection, self.state.clone());
        state.cros_ctrl_connected = true;
        Some(Box::new(AddEventSourceMutator::from(handler)))
    }
}

fn main() {
    if let Err(e) = syslog::init() {
        eprintln!("Failed to initialize syslog: {}", e);
        return;
    }
    info!("starting ManaTEE memory service: {}", BUILD_TIMESTAMP);

    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        error!("Usage: manatee_memory_service <CrOS guest memory in MiB>");
        return;
    }
    let cros_mem = match args[1].parse::<u64>() {
        Ok(cros_mem) => match cros_mem.checked_mul(1024 * 1024) {
            Some(cros_mem) => cros_mem,
            None => {
                error!("Cros memory size overflow: {}", cros_mem);
                return;
            }
        },
        Err(e) => {
            error!("Error parsing cros memory size: {:?}", e);
            return;
        }
    };

    let crosvm_server = match UnixServerTransport::new(&get_control_server_path(CROS_GUEST_ID)) {
        Ok(server) => server,
        Err(e) => {
            error!("Failed to start cros guest server {:?}", e);
            return;
        }
    };
    let crosvm_client = match crosvm_server.accept_with_timeout(Duration::MAX) {
        Ok(client) => client,
        Err(e) => {
            error!("Failed to connect to cros guest balloon {:?}", e);
            return;
        }
    };
    cleanup_control_server(CROS_GUEST_ID, crosvm_server);
    let mut clients = HashMap::new();
    clients.insert(
        CROS_GUEST_ID,
        CrosVmClient {
            client: crosvm_client,
            mem_size: cros_mem,
            balloon_size: 0,
        },
    );

    let state = Rc::new(RefCell::new(MmsState {
        cros_ctrl_connected: false,
        clients,
    }));

    let ctrl_connection = TransportType::VsockConnection(VSocketAddr {
        cid: VsockCid::Any,
        port: DEFAULT_MMS_PORT,
    });
    let ctrl_server =
        TransportServer::new(&ctrl_connection, CtrlServerHandler::new(state)).unwrap();

    let mut ctx = EventMultiplexer::new().unwrap();
    ctx.add_event(Box::new(ctrl_server)).unwrap();
    while !ctx.is_empty() {
        if let Err(e) = ctx.run_once() {
            error!("{}", e);
        };
    }

    info!("ManaTEE memory service exiting");
}
