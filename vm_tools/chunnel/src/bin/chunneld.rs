// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::btree_map::Entry as BTreeMapEntry;
use std::collections::{BTreeMap, BTreeSet, HashMap, VecDeque};
use std::ffi::CStr;
use std::fmt;
use std::io;
use std::mem;
use std::net::{Ipv4Addr, Ipv6Addr, TcpListener};
use std::os::raw::c_int;
use std::os::unix::io::AsRawFd;
use std::result;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use dbus::{self, tree, Connection as DBusConnection, Error as DBusError, Message as DBusMessage};
use libchromeos::syslog;
use libchromeos::vsock::{VsockListener, VMADDR_PORT_ANY};
use log::{error, warn};
use protobuf::{self, Message as ProtoMessage, ProtobufError};
use sys_util::{self, block_signal, EventFd, PollContext, PollToken};

use chunnel::forwarder::ForwarderSession;
use system_api::chunneld_service::*;
use system_api::cicerone_service;

// chunnel dbus-constants.h
const CHUNNELD_INTERFACE: &str = "org.chromium.Chunneld";
const CHUNNELD_SERVICE_PATH: &str = "/org/chromium/Chunneld";
const CHUNNELD_SERVICE_NAME: &str = "org.chromium.Chunneld";

// cicerone dbus-constants.h
const VM_CICERONE_INTERFACE: &str = "org.chromium.VmCicerone";
const VM_CICERONE_SERVICE_PATH: &str = "/org/chromium/VmCicerone";
const VM_CICERONE_SERVICE_NAME: &str = "org.chromium.VmCicerone";
const CONNECT_CHUNNEL_METHOD: &str = "ConnectChunnel";

// chunneld dbus-constants.h
const UPDATE_LISTENING_PORTS_METHOD: &str = "UpdateListeningPorts";

const CHUNNEL_CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
const DBUS_TIMEOUT_MS: i32 = 30 * 1000;

// Program name.
const IDENT: &[u8] = b"chunneld\0";

#[remain::sorted]
#[derive(Debug)]
enum Error {
    BindVsock(io::Error),
    BlockSigpipe(sys_util::signal::Error),
    ConnectChunnelFailure(String),
    CreateProtobusService(dbus::Error),
    DBusCreateMethodCall(String),
    DBusGetSystemBus(DBusError),
    DBusMessageRead(dbus::arg::TypeMismatchError),
    DBusMessageSend(DBusError),
    DBusRegisterTree(DBusError),
    EventFdClone(sys_util::Error),
    EventFdNew(sys_util::Error),
    IncorrectCid(u32),
    NoListenerForPort(u16),
    NoSessionForTag(SessionTag),
    PollContextAdd(sys_util::Error),
    PollContextDelete(sys_util::Error),
    PollContextNew(sys_util::Error),
    PollWait(sys_util::Error),
    ProtobufDeserialize(ProtobufError),
    ProtobufSerialize(ProtobufError),
    SetVsockNonblocking(io::Error),
    Syslog(log::SetLoggerError),
    TcpAccept(io::Error),
    TcpListenerPort(io::Error),
    UpdateEventRead(sys_util::Error),
    VsockAccept(io::Error),
    VsockAcceptTimeout,
    VsockListenerPort(io::Error),
}

type Result<T> = result::Result<T, Error>;

impl fmt::Display for Error {
    #[remain::check]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        #[remain::sorted]
        match self {
            BindVsock(e) => write!(f, "failed to bind vsock: {}", e),
            BlockSigpipe(e) => write!(f, "failed to block SIGPIPE: {}", e),
            ConnectChunnelFailure(e) => write!(f, "failed to connect chunnel: {}", e),
            CreateProtobusService(e) => write!(f, "failed to create D-Bus service: {}", e),
            DBusCreateMethodCall(e) => write!(f, "failed to create D-Bus method call: {}", e),
            DBusGetSystemBus(e) => write!(f, "failed to get D-Bus system bus: {}", e),
            DBusMessageRead(e) => write!(f, "failed to read D-Bus message: {}", e),
            DBusMessageSend(e) => write!(f, "failed to send D-Bus message: {}", e),
            DBusRegisterTree(e) => write!(f, "failed to register D-Bus tree: {}", e),
            EventFdClone(e) => write!(f, "failed to clone eventfd: {}", e),
            EventFdNew(e) => write!(f, "failed to create eventfd: {}", e),
            IncorrectCid(cid) => write!(f, "chunnel connection from unexpected cid {}", cid),
            NoListenerForPort(port) => write!(f, "could not find listener for port: {}", port),
            NoSessionForTag(tag) => write!(f, "could not find session for tag: {:x}", tag),
            PollContextAdd(e) => write!(f, "failed to add fd to poll context: {}", e),
            PollContextDelete(e) => write!(f, "failed to delete fd from poll context: {}", e),
            PollContextNew(e) => write!(f, "failed to create poll context: {}", e),
            PollWait(e) => write!(f, "failed to wait for poll: {}", e),
            ProtobufDeserialize(e) => write!(f, "failed to deserialize protobuf: {}", e),
            ProtobufSerialize(e) => write!(f, "failed to serialize protobuf: {}", e),
            SetVsockNonblocking(e) => write!(f, "failed to set vsock to nonblocking: {}", e),
            Syslog(e) => write!(f, "failed to initialize syslog: {}", e),
            TcpAccept(e) => write!(f, "failed to accept tcp: {}", e),
            TcpListenerPort(e) => {
                write!(f, "failed to read local sockaddr for tcp listener: {}", e)
            }
            UpdateEventRead(e) => write!(f, "failed to read update eventfd: {}", e),
            VsockAccept(e) => write!(f, "failed to accept vsock: {}", e),
            VsockAcceptTimeout => write!(f, "timed out waiting for vsock connection"),
            VsockListenerPort(e) => write!(f, "failed to get vsock listener port: {}", e),
        }
    }
}

/// A TCP forwarding target. Uniquely identifies a listening port in a given container.
struct TcpForwardTarget {
    pub port: u16,
    pub vm_name: String,
    pub container_name: String,
    pub owner_id: String,
    pub vsock_cid: u32,
}

/// A tag that uniquely identifies a particular forwarding session. This has arbitrarily been
/// chosen as the fd of the local (TCP) socket.
type SessionTag = u32;

/// Implements PollToken for chunneld's main poll loop.
#[derive(Clone, Copy, PollToken)]
enum Token {
    UpdatePorts,
    Ipv4Listener(u16),
    Ipv6Listener(u16),
    LocalSocket(SessionTag),
    RemoteSocket(SessionTag),
}

/// PortListeners includes all listeners (IPv4 and IPv6) for a given port, and the target
/// container.
struct PortListeners {
    tcp4_listener: TcpListener,
    tcp6_listener: TcpListener,
    forward_target: TcpForwardTarget,
}

/// SocketFamily specifies whether a socket uses IPv4 or IPv6.
enum SocketFamily {
    Ipv4,
    Ipv6,
}

/// ForwarderSessions encapsulates all forwarding state for chunneld.
struct ForwarderSessions {
    listening_ports: BTreeMap<u16, PortListeners>,
    tcp4_forwarders: HashMap<SessionTag, ForwarderSession>,
    update_evt: EventFd,
    update_queue: Arc<Mutex<VecDeque<TcpForwardTarget>>>,
    dbus_conn: DBusConnection,
}

impl ForwarderSessions {
    /// Creates a new instance of ForwarderSessions.
    fn new(
        update_evt: EventFd,
        update_queue: Arc<Mutex<VecDeque<TcpForwardTarget>>>,
    ) -> Result<Self> {
        Ok(ForwarderSessions {
            listening_ports: BTreeMap::new(),
            tcp4_forwarders: HashMap::new(),
            update_evt,
            update_queue,
            dbus_conn: DBusConnection::get_private(dbus::BusType::System)
                .map_err(Error::DBusGetSystemBus)?,
        })
    }

    /// Adds or removes listeners based on the latest listening ports from the D-Bus thread.
    fn process_update_queue(&mut self, poll_ctx: &PollContext<Token>) -> Result<()> {
        // Unwrap of LockResult is customary.
        let mut update_queue = self.update_queue.lock().unwrap();
        let mut active_ports: BTreeSet<u16> = BTreeSet::new();

        // Add any new listeners first.
        while let Some(target) = update_queue.pop_front() {
            let port = target.port;
            // Ignore privileged ports.
            if port < 1024 {
                continue;
            }
            if let BTreeMapEntry::Vacant(o) = self.listening_ports.entry(port) {
                // Failing to bind a port is not fatal, but we should log it.
                // Both IPv4 and IPv6 localhost must be bound since the host may resolve
                // "localhost" to either.
                let tcp4_listener = match TcpListener::bind((Ipv4Addr::LOCALHOST, port)) {
                    Ok(listener) => listener,
                    Err(e) => {
                        warn!("failed to bind TCPv4 port: {}", e);
                        continue;
                    }
                };
                let tcp6_listener = match TcpListener::bind((Ipv6Addr::LOCALHOST, port)) {
                    Ok(listener) => listener,
                    Err(e) => {
                        warn!("failed to bind TCPv6 port: {}", e);
                        continue;
                    }
                };
                poll_ctx
                    .add_many(&[
                        (&tcp4_listener, Token::Ipv4Listener(port)),
                        (&tcp6_listener, Token::Ipv6Listener(port)),
                    ])
                    .map_err(Error::PollContextAdd)?;
                o.insert(PortListeners {
                    tcp4_listener,
                    tcp6_listener,
                    forward_target: target,
                });
            }
            active_ports.insert(port);
        }

        // Iterate over the existing listeners; if the port is no longer in the
        // listener list, remove it.
        let old_ports: Vec<u16> = self.listening_ports.keys().cloned().collect();
        for port in old_ports.iter() {
            if !active_ports.contains(port) {
                self.listening_ports.remove(&port);
            }
        }

        // Consume the eventfd.
        self.update_evt.read().map_err(Error::UpdateEventRead)?;

        Ok(())
    }

    fn accept_connection(
        &mut self,
        poll_ctx: &PollContext<Token>,
        port: u16,
        sock_family: SocketFamily,
    ) -> Result<()> {
        let port_listeners = self
            .listening_ports
            .get(&port)
            .ok_or(Error::NoListenerForPort(port))?;

        let listener = match sock_family {
            SocketFamily::Ipv4 => &port_listeners.tcp4_listener,
            SocketFamily::Ipv6 => &port_listeners.tcp6_listener,
        };

        // This session should be dropped if any of the PollContext setup fails. Since the only
        // extant fds for the underlying sockets will be closed, they will be unregistered from
        // epoll set automatically.
        let session = create_forwarder_session(
            &mut self.dbus_conn,
            listener,
            &port_listeners.forward_target,
        )?;

        let tag = session.local_stream().as_raw_fd() as u32;

        poll_ctx
            .add_many(&[
                (session.local_stream(), Token::LocalSocket(tag)),
                (session.remote_stream(), Token::RemoteSocket(tag)),
            ])
            .map_err(Error::PollContextAdd)?;

        self.tcp4_forwarders.insert(tag, session);

        Ok(())
    }

    fn forward_from_local(&mut self, poll_ctx: &PollContext<Token>, tag: SessionTag) -> Result<()> {
        let session = self
            .tcp4_forwarders
            .get_mut(&tag)
            .ok_or(Error::NoSessionForTag(tag))?;
        let shutdown = session.forward_from_local().unwrap_or(true);
        if shutdown {
            poll_ctx
                .delete(session.local_stream())
                .map_err(Error::PollContextDelete)?;
            if session.is_shut_down() {
                self.tcp4_forwarders.remove(&tag);
            }
        }

        Ok(())
    }

    fn forward_from_remote(
        &mut self,
        poll_ctx: &PollContext<Token>,
        tag: SessionTag,
    ) -> Result<()> {
        let session = self
            .tcp4_forwarders
            .get_mut(&tag)
            .ok_or(Error::NoSessionForTag(tag))?;
        let shutdown = session.forward_from_remote().unwrap_or(true);
        if shutdown {
            poll_ctx
                .delete(session.remote_stream())
                .map_err(Error::PollContextDelete)?;
            if session.is_shut_down() {
                self.tcp4_forwarders.remove(&tag);
            }
        }

        Ok(())
    }

    fn run(&mut self) -> Result<()> {
        let poll_ctx: PollContext<Token> =
            PollContext::build_with(&[(&self.update_evt, Token::UpdatePorts)])
                .map_err(Error::PollContextNew)?;

        loop {
            let events = poll_ctx.wait().map_err(Error::PollWait)?;

            for event in events.iter_readable() {
                match event.token() {
                    Token::UpdatePorts => {
                        if let Err(e) = self.process_update_queue(&poll_ctx) {
                            error!("error updating listening ports: {}", e);
                        }
                    }
                    Token::Ipv4Listener(port) => {
                        if let Err(e) = self.accept_connection(&poll_ctx, port, SocketFamily::Ipv4)
                        {
                            error!("error accepting connection: {}", e);
                        }
                    }
                    Token::Ipv6Listener(port) => {
                        if let Err(e) = self.accept_connection(&poll_ctx, port, SocketFamily::Ipv6)
                        {
                            error!("error accepting connection: {}", e);
                        }
                    }
                    Token::LocalSocket(tag) => {
                        if let Err(e) = self.forward_from_local(&poll_ctx, tag) {
                            error!("error forwarding local traffic: {}", e);
                        }
                    }
                    Token::RemoteSocket(tag) => {
                        if let Err(e) = self.forward_from_remote(&poll_ctx, tag) {
                            error!("error forwarding remote traffic: {}", e);
                        }
                    }
                }
            }
        }
    }
}

/// Sends a D-Bus request to launch chunnel in the target container.
fn launch_chunnel(
    dbus_conn: &mut DBusConnection,
    vsock_port: u32,
    tcp4_port: u16,
    target: &TcpForwardTarget,
) -> Result<()> {
    let mut request = cicerone_service::ConnectChunnelRequest::new();
    request.vm_name = target.vm_name.to_owned();
    request.container_name = target.container_name.to_owned();
    request.owner_id = target.owner_id.to_owned();
    request.chunneld_port = vsock_port;
    request.target_tcp4_port = u32::from(tcp4_port);

    let dbus_request = DBusMessage::new_method_call(
        VM_CICERONE_SERVICE_NAME,
        VM_CICERONE_SERVICE_PATH,
        VM_CICERONE_INTERFACE,
        CONNECT_CHUNNEL_METHOD,
    )
    .map_err(Error::DBusCreateMethodCall)?
    .append1(request.write_to_bytes().map_err(Error::ProtobufSerialize)?);
    let dbus_reply = dbus_conn
        .send_with_reply_and_block(dbus_request, DBUS_TIMEOUT_MS)
        .map_err(Error::DBusMessageSend)?;
    let raw_buffer: Vec<u8> = dbus_reply.read1().map_err(Error::DBusMessageRead)?;
    let response: cicerone_service::ConnectChunnelResponse =
        protobuf::parse_from_bytes(&raw_buffer).map_err(Error::ProtobufDeserialize)?;

    match response.status {
        cicerone_service::ConnectChunnelResponse_Status::SUCCESS => Ok(()),
        _ => Err(Error::ConnectChunnelFailure(response.failure_reason)),
    }
}

/// Creates a forwarder session from a `listener` that has a pending connection to accept.
fn create_forwarder_session(
    dbus_conn: &mut DBusConnection,
    listener: &TcpListener,
    target: &TcpForwardTarget,
) -> Result<ForwarderSession> {
    let (tcp_stream, _) = listener.accept().map_err(Error::TcpAccept)?;
    // Bind a vsock port, tell the guest to connect, and accept the connection.
    let mut vsock_listener = VsockListener::bind(VMADDR_PORT_ANY).map_err(Error::BindVsock)?;
    vsock_listener
        .set_nonblocking(true)
        .map_err(Error::SetVsockNonblocking)?;

    let tcp4_port = listener
        .local_addr()
        .map_err(Error::TcpListenerPort)?
        .port();

    launch_chunnel(
        dbus_conn,
        vsock_listener
            .local_port()
            .map_err(Error::VsockListenerPort)?,
        tcp4_port,
        target,
    )?;

    #[derive(PollToken)]
    enum Token {
        VsockAccept,
    }

    let poll_ctx: PollContext<Token> =
        PollContext::build_with(&[(&vsock_listener, Token::VsockAccept)])
            .map_err(Error::PollContextNew)?;

    // Wait a few seconds for the guest to connect.
    let events = poll_ctx
        .wait_timeout(CHUNNEL_CONNECT_TIMEOUT)
        .map_err(Error::PollWait)?;

    match events.iter_readable().next() {
        Some(_) => {
            let (vsock_stream, sockaddr) = vsock_listener.accept().map_err(Error::VsockAccept)?;

            if sockaddr.cid != target.vsock_cid {
                Err(Error::IncorrectCid(sockaddr.cid))
            } else {
                Ok(ForwarderSession::new(
                    tcp_stream.into(),
                    vsock_stream.into(),
                ))
            }
        }
        None => Err(Error::VsockAcceptTimeout),
    }
}

/// Enqueues the new listening ports received over D-Bus for the main worker thread to process.
fn update_listening_ports(
    req: &mut UpdateListeningPortsRequest,
    update_queue: &Arc<Mutex<VecDeque<TcpForwardTarget>>>,
    update_evt: &EventFd,
) -> UpdateListeningPortsResponse {
    let mut response = UpdateListeningPortsResponse::new();

    // Unwrap of LockResult is customary.
    let mut update_queue = update_queue.lock().unwrap();

    for (forward_port, forward_target) in req.mut_tcp4_forward_targets().iter_mut() {
        update_queue.push_back(TcpForwardTarget {
            port: *forward_port as u16,
            vm_name: forward_target.take_vm_name(),
            owner_id: forward_target.take_owner_id(),
            container_name: forward_target.take_container_name(),
            vsock_cid: forward_target.vsock_cid,
        });
    }

    match update_evt.write(1) {
        Ok(_) => {
            response.set_status(UpdateListeningPortsResponse_Status::SUCCESS);
        }
        Err(_) => {
            response.set_status(UpdateListeningPortsResponse_Status::FAILED);
        }
    }

    response
}

/// Sets up the D-Bus object paths and runs the D-Bus loop.
fn dbus_thread(
    update_queue: Arc<Mutex<VecDeque<TcpForwardTarget>>>,
    update_evt: EventFd,
) -> Result<()> {
    let connection = dbus::Connection::get_private(dbus::BusType::System)
        .map_err(Error::CreateProtobusService)?;

    connection
        .register_name(CHUNNELD_SERVICE_NAME, 0)
        .map_err(Error::CreateProtobusService)?;

    let f = tree::Factory::new_fnmut::<()>();
    let dbus_interface = f.interface(CHUNNELD_INTERFACE, ());
    let dbus_method = f
        .method(UPDATE_LISTENING_PORTS_METHOD, (), move |m| {
            let reply = m.msg.method_return();
            let raw_buf: Vec<u8> = m.msg.read1().map_err(|_| tree::MethodErr::no_arg())?;
            let mut proto: UpdateListeningPortsRequest = protobuf::parse_from_bytes(&raw_buf)
                .map_err(|e| tree::MethodErr::invalid_arg(&e))?;

            let response = update_listening_ports(&mut proto, &update_queue, &update_evt);
            Ok(vec![reply.append1(
                response
                    .write_to_bytes()
                    .map_err(|e| tree::MethodErr::failed(&e))?,
            )])
        })
        .in_arg("ay")
        .out_arg("ay");
    let t = f.tree(()).add(
        f.object_path(CHUNNELD_SERVICE_PATH, ())
            .introspectable()
            .add(dbus_interface.add_m(dbus_method)),
    );

    t.set_registered(&connection, true)
        .map_err(Error::DBusRegisterTree)?;
    connection.add_handler(t);

    // The D-Bus crate uses a u32 for the timeout, but D-Bus internally uses c_int with -1 being
    // infinite. We don't want chunneld waking frequently, so use -1 if c_int is the same size
    // as u32, and a big value otherwise.
    let timeout: u32 = if mem::size_of::<u32>() == mem::size_of::<c_int>() {
        -1i32 as u32
    } else {
        c_int::max_value() as u32
    };
    loop {
        connection.incoming(timeout).next();
    }
}

fn main() -> Result<()> {
    // Safe because this string is defined above in this file and it contains exactly
    // one nul byte, which appears at the end.
    let ident = CStr::from_bytes_with_nul(IDENT).unwrap();
    syslog::init(ident).map_err(Error::Syslog)?;

    // Block SIGPIPE so the process doesn't exit when writing to a socket that's been shutdown.
    block_signal(libc::SIGPIPE).map_err(Error::BlockSigpipe)?;

    let update_evt = EventFd::new().map_err(Error::EventFdNew)?;
    let update_queue = Arc::new(Mutex::new(VecDeque::new()));
    let dbus_update_queue = update_queue.clone();

    let worker_update_evt = update_evt.try_clone().map_err(Error::EventFdClone)?;
    let _ = thread::Builder::new()
        .name("chunnel_dbus".to_string())
        .spawn(move || {
            match dbus_thread(dbus_update_queue, worker_update_evt) {
                Ok(()) => error!("D-Bus thread has exited unexpectedly"),
                Err(e) => error!("D-Bus thread has exited with err {}", e),
            };
        });

    let mut sessions = ForwarderSessions::new(update_evt, update_queue)?;
    sessions.run()
}
