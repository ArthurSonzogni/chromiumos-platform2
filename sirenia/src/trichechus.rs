// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A TEE application life-cycle manager.

use std::io::stderr;
use std::os::unix::io::AsRawFd;

use std::cell::RefCell;
use std::collections::{HashMap, VecDeque};
use std::env;
use std::fmt::Debug;
use std::mem::swap;
use std::os::unix::io::RawFd;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::result::Result as StdResult;

use getopts::Options;
use libsirenia::cli::TransportTypeOption;
use libsirenia::communication::persistence::{Cronista, CronistaClient, Status};
use libsirenia::communication::{StorageRpc, StorageRpcServer};
use libsirenia::linux::events::{AddEventSourceMutator, EventMultiplexer, Mutator};
use libsirenia::linux::syslog::{Syslog, SyslogReceiverMut, SYSLOG_PATH};
use libsirenia::rpc::{ConnectionHandler, RpcDispatcher, TransportServer};
use libsirenia::sandbox::{self, Sandbox};
use libsirenia::to_sys_util;
use libsirenia::transport::{
    self, create_transport_from_pipes, Transport, TransportType, CROS_CID, CROS_CONNECTION_ERR_FD,
    CROS_CONNECTION_R_FD, CROS_CONNECTION_W_FD, DEFAULT_CLIENT_PORT, DEFAULT_CONNECTION_R_FD,
    DEFAULT_CONNECTION_W_FD, DEFAULT_CRONISTA_PORT, DEFAULT_SERVER_PORT,
};
use sirenia::app_info::{self, AppManifest, AppManifestEntry, SandboxType};
use sirenia::build_info::BUILD_TIMESTAMP;
use sirenia::cli::initialize_common_arguments;
use sirenia::communication::{AppInfo, Trichechus, TrichechusServer};
use sys_util::vsock::SocketAddr as VSocketAddr;
use sys_util::{self, error, getpid, getsid, info, setsid, syslog};
use thiserror::Error as ThisError;

const CRONISTA_URI_SHORT_NAME: &str = "C";
const CRONISTA_URI_LONG_NAME: &str = "cronista";
const SYSLOG_PATH_SHORT_NAME: &str = "L";

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed to initialize the syslog: {0}")]
    InitSyslog(sys_util::syslog::Error),
    #[error("failed to open pipe: {0}")]
    OpenPipe(sys_util::Error),
    #[error("failed create transport: {0}")]
    NewTransport(transport::Error),
    #[error("got unexpected transport type: {0:?}")]
    UnexpectedConnectionType(TransportType),
    #[error("failed to create new sandbox: {0}")]
    NewSandbox(sandbox::Error),
    #[error("failed to start up sandbox: {0}")]
    RunSandbox(sandbox::Error),
    #[error("received unexpected request type")]
    UnexpectedRequest,
    #[error("Error retrieving from  app_manifest: {0}")]
    AppManifest(app_info::Error),
    #[error("Sandbox type not implemented for: {0:?}")]
    SandboxTypeNotImplemented(AppManifestEntry),
}

/// The result of an operation in this crate.
pub type Result<T> = StdResult<T, Error>;

/* Holds the trichechus-relevant information for a TEEApp. */
struct TeeApp {
    _sandbox: Sandbox,
    app_info: AppManifestEntry,
}

#[derive(Clone)]
struct TeeAppHandler {
    state: Rc<RefCell<TrichechusState>>,
    tee_app: Rc<RefCell<TeeApp>>,
}

impl StorageRpc for TeeAppHandler {
    type Error = ();

    fn read_data(&self, id: String) -> StdResult<(Status, Vec<u8>), Self::Error> {
        let app_info = &self.tee_app.borrow().app_info;
        let params = app_info.storage_parameters.as_ref().ok_or_else(|| {
            error!(
                "App id '{}' made an unconfigured call to the read_data storage API.",
                &app_info.app_name
            );
        })?;
        let mut state = self.state.borrow_mut();

        // If already connected try once, to see if the connection dropped.
        if let Some(persistence) = &state.persistence {
            let ret =
                persistence.retrieve(params.scope.clone(), params.domain.to_string(), id.clone());
            match ret {
                Err(err) => {
                    // If the client is no longer valid, drop it so it will be recreated on the next call.
                    state.persistence = None;
                    error!("failed to retrieve data: {}", err);
                }
                Ok(a) => return Ok(a),
            }
        }

        let persistence = state.check_persistence().map_err(|err| {
            error!("failed to retrieve data: {}", err);
        })?;

        let ret = persistence.retrieve(params.scope.clone(), params.domain.to_string(), id);
        ret.map_err(|err| {
            // If the client is no longer valid, drop it so it will be recreated on the next call.
            state.persistence = None;
            error!("failed to retrieve data: {}", err);
        })
    }

    fn write_data(&self, id: String, data: Vec<u8>) -> StdResult<Status, Self::Error> {
        let app_info = &self.tee_app.borrow().app_info;
        let params = app_info.storage_parameters.as_ref().ok_or_else(|| {
            error!(
                "App id '{}' made an unconfigured call to the write_data storage API.",
                &app_info.app_name
            );
        })?;
        let mut state = self.state.borrow_mut();

        // If already connected try once, to see if the connection dropped.
        if let Some(persistence) = &state.persistence {
            let ret = persistence.persist(
                params.scope.clone(),
                params.domain.to_string(),
                id.clone(),
                data.clone(),
            );

            match ret {
                Err(err) => {
                    // If the client is no longer valid, drop it so it will be recreated on the next call.
                    state.persistence = None;
                    error!("failed to persist data: {}", err);
                }
                Ok(a) => return Ok(a),
            }
        }

        let persistence = state.check_persistence().map_err(|err| {
            error!("failed to persist data: {}", err);
        })?;

        let ret = persistence.persist(params.scope.clone(), params.domain.to_string(), id, data);

        ret.map_err(|err| {
            // If the client is no longer valid, drop it so it will be recreated on the next call.
            state.persistence = None;
            error!("failed to persist data: {}", err);
        })
    }
}

struct TrichechusState {
    expected_port: u32,
    pending_apps: HashMap<TransportType, String>,
    running_apps: HashMap<TransportType, Rc<RefCell<TeeApp>>>,
    log_queue: VecDeque<Vec<u8>>,
    persistence_uri: TransportType,
    persistence: Option<CronistaClient>,
    app_manifest: AppManifest,
}

impl TrichechusState {
    fn new() -> Self {
        TrichechusState {
            expected_port: DEFAULT_CLIENT_PORT,
            pending_apps: HashMap::new(),
            running_apps: HashMap::new(),
            log_queue: VecDeque::new(),
            persistence_uri: TransportType::VsockConnection(VSocketAddr {
                cid: CROS_CID,
                port: DEFAULT_CRONISTA_PORT,
            }),
            persistence: None,
            app_manifest: AppManifest::new(),
        }
    }

    fn check_persistence(&mut self) -> Result<&mut CronistaClient> {
        let uri = self.persistence_uri.clone();
        if self.persistence.is_none() {
            self.persistence = Some(CronistaClient::new(
                uri.try_into_client(None)
                    .unwrap()
                    .connect()
                    .map_err(Error::NewTransport)?,
            ));
        }
        Ok(self.persistence.as_mut().unwrap())
    }
}

impl SyslogReceiverMut for TrichechusState {
    fn receive(&mut self, data: Vec<u8>) {
        self.log_queue.push_back(data);
    }
}

#[derive(Clone)]
struct TrichechusServerImpl {
    state: Rc<RefCell<TrichechusState>>,
    transport_type: TransportType,
}

impl TrichechusServerImpl {
    fn new(state: Rc<RefCell<TrichechusState>>, transport_type: TransportType) -> Self {
        TrichechusServerImpl {
            state,
            transport_type,
        }
    }

    fn port_to_transport_type(&self, port: u32) -> TransportType {
        let mut result = self.transport_type.clone();
        match &mut result {
            TransportType::IpConnection(addr) => addr.set_port(port as u16),
            TransportType::VsockConnection(addr) => {
                addr.port = port;
            }
            _ => panic!("unexpected connection type"),
        }
        result
    }
}

impl Trichechus for TrichechusServerImpl {
    type Error = ();

    fn start_session(&self, app_info: AppInfo) -> StdResult<(), ()> {
        info!("Received start session message: {:?}", &app_info);
        // The TEE app isn't started until its socket connection is accepted.
        self.state.borrow_mut().pending_apps.insert(
            self.port_to_transport_type(app_info.port_number),
            app_info.app_id,
        );
        Ok(())
    }

    fn get_logs(&self) -> StdResult<Vec<Vec<u8>>, ()> {
        let mut replacement: VecDeque<Vec<u8>> = VecDeque::new();
        swap(&mut self.state.borrow_mut().log_queue, &mut replacement);
        Ok(replacement.into())
    }
}

struct DugongConnectionHandler {
    state: Rc<RefCell<TrichechusState>>,
}

impl DugongConnectionHandler {
    fn new(state: Rc<RefCell<TrichechusState>>) -> Self {
        DugongConnectionHandler { state }
    }

    fn connect_tee_app(&mut self, app_id: &str, connection: Transport) -> Option<Box<dyn Mutator>> {
        let id = connection.id.clone();
        let state = self.state.clone();
        // Only borrow once.
        let mut trichechus_state = self.state.borrow_mut();
        match spawn_tee_app(&trichechus_state.app_manifest, app_id, connection) {
            Ok((app, transport)) => {
                let tee_app = Rc::new(RefCell::new(app));
                trichechus_state.running_apps.insert(id, tee_app.clone());
                let storage_server: Box<dyn StorageRpcServer> =
                    Box::new(TeeAppHandler { state, tee_app });
                Some(Box::new(AddEventSourceMutator(Some(Box::new(
                    RpcDispatcher::new(storage_server, transport),
                )))))
            }
            Err(e) => {
                error!("failed to start tee app: {}", e);
                None
            }
        }
    }
}

impl ConnectionHandler for DugongConnectionHandler {
    fn handle_incoming_connection(&mut self, connection: Transport) -> Option<Box<dyn Mutator>> {
        info!("incoming connection '{:?}'", &connection.id);
        let expected_port = self.state.borrow().expected_port;
        // Check if the incoming connection is expected and associated with a TEE
        // application.
        let reservation = self.state.borrow_mut().pending_apps.remove(&connection.id);
        if let Some(app_id) = reservation {
            info!("starting instance of '{}'", app_id);
            self.connect_tee_app(&app_id, connection)
        } else {
            // Check if it is a control connection.
            match connection.id.get_port() {
                Ok(port) if port == expected_port => {
                    info!("new control connection.");
                    Some(Box::new(AddEventSourceMutator(Some(Box::new(
                        RpcDispatcher::new(
                            TrichechusServerImpl::new(self.state.clone(), connection.id.clone())
                                .box_clone(),
                            connection,
                        ),
                    )))))
                }
                _ => {
                    error!("dropping unexpected connection.");
                    None
                }
            }
        }
    }
}

fn spawn_tee_app(
    app_manifest: &AppManifest,
    app_id: &str,
    transport: Transport,
) -> Result<(TeeApp, Transport)> {
    let app_info = app_manifest
        .get_app_manifest_entry(app_id)
        .map_err(Error::AppManifest)?;
    let mut sandbox = match &app_info.sandbox_type {
        SandboxType::DeveloperEnvironment => Sandbox::passthrough().map_err(Error::NewSandbox)?,
        SandboxType::Container => Sandbox::new(None).map_err(Error::NewSandbox)?,
        SandboxType::VirtualMachine => {
            return Err(Error::SandboxTypeNotImplemented(app_info.to_owned()))
        }
    };
    let (trichechus_transport, tee_transport) =
        create_transport_from_pipes().map_err(Error::NewTransport)?;
    let keep_fds: [(RawFd, RawFd); 5] = [
        (transport.r.as_raw_fd(), CROS_CONNECTION_R_FD),
        (transport.w.as_raw_fd(), CROS_CONNECTION_W_FD),
        (stderr().as_raw_fd(), CROS_CONNECTION_ERR_FD),
        (tee_transport.r.as_raw_fd(), DEFAULT_CONNECTION_R_FD),
        (tee_transport.w.as_raw_fd(), DEFAULT_CONNECTION_W_FD),
    ];
    let process_path = app_info.path.to_string();

    sandbox
        .run(Path::new(&process_path), &[&process_path], &keep_fds)
        .map_err(Error::RunSandbox)?;

    Ok((
        TeeApp {
            _sandbox: sandbox,
            app_info: app_info.to_owned(),
        },
        trichechus_transport,
    ))
}

// TODO: Figure out how to clean up TEEs that are no longer in use
// TODO: Figure out rate limiting and prevention against DOS attacks
// TODO: What happens if dugong crashes? How do we want to handle
fn main() -> Result<()> {
    // Handle the arguments first since "-h" shouldn't have any side effects on the system such as
    // creating /dev/log.
    let args: Vec<String> = env::args().collect();
    let mut opts = Options::new();
    opts.optopt(
        SYSLOG_PATH_SHORT_NAME,
        "syslog-path",
        "connect to trichechus, get and print logs, then exit.",
        SYSLOG_PATH,
    );
    let cronista_uri_option = TransportTypeOption::new(
        CRONISTA_URI_SHORT_NAME,
        CRONISTA_URI_LONG_NAME,
        "URI to connect to cronista",
        "vsock://3:5554",
        &mut opts,
    );
    let (config, matches) = initialize_common_arguments(opts, &args[1..]).unwrap();
    let state = Rc::new(RefCell::new(TrichechusState::new()));

    // Create /dev/log if it doesn't already exist since trichechus is the first thing to run after
    // the kernel on the hypervisor.
    let log_path = PathBuf::from(
        matches
            .opt_str(SYSLOG_PATH_SHORT_NAME)
            .unwrap_or_else(|| SYSLOG_PATH.to_string()),
    );
    let syslog: Option<Syslog> = if !log_path.exists() {
        eprintln!("Creating syslog.");
        Some(Syslog::new(log_path, state.clone()).unwrap())
    } else {
        eprintln!("Syslog exists.");
        None
    };

    // Before logging is initialized eprintln(...) and println(...) should be used. Afterward,
    // info!(...), and error!(...) should be used instead.
    if let Err(e) = syslog::init() {
        eprintln!("Failed to initialize syslog: {}", e);
        return Err(Error::InitSyslog(e));
    }
    info!("starting trichechus: {}", BUILD_TIMESTAMP);

    if getpid() != getsid(None).unwrap() {
        if let Err(err) = setsid() {
            error!("Unable to start new process group: {}", err);
        }
    }
    to_sys_util::block_all_signals();
    // This is safe because no additional file descriptors have been opened (except syslog which
    // cannot be dropped until we are ready to clean up /dev/log).
    let ret = unsafe { to_sys_util::fork() }.unwrap();
    if ret != 0 {
        // The parent process collects the return codes from the child processes, so they do not
        // remain zombies.
        while to_sys_util::wait_for_child() {}
        info!("reaper done!");
        return Ok(());
    }

    // Unblock signals for the process that spawns the children. It might make sense to fork
    // again here for each child to avoid them blocking each other.
    to_sys_util::unblock_all_signals();

    if let Some(uri) = cronista_uri_option.from_matches(&matches).unwrap() {
        let mut state_mut = state.borrow_mut();
        state_mut.persistence_uri = uri.clone();
        state_mut.persistence = Some(CronistaClient::new(
            uri.try_into_client(None).unwrap().connect().unwrap(),
        ));
    }

    let mut ctx = EventMultiplexer::new().unwrap();
    if let Some(event_source) = syslog {
        ctx.add_event(Box::new(event_source)).unwrap();
    }

    let server = TransportServer::new(
        &config.connection_type,
        DugongConnectionHandler::new(state.clone()),
    )
    .unwrap();
    let listen_addr = server.bound_to();
    ctx.add_event(Box::new(server)).unwrap();

    // Handle parent dugong connection.
    if let Ok(addr) = listen_addr {
        // Adjust the expected port when binding to an ephemeral port to facilitate testing.
        match addr.get_port() {
            Ok(DEFAULT_SERVER_PORT) | Err(_) => {}
            Ok(port) => {
                state.borrow_mut().expected_port = port + 1;
            }
        }
        info!("waiting for connection at: {}", addr);
    } else {
        info!("waiting for connection");
    }
    while !ctx.is_empty() {
        if let Err(e) = ctx.run_once() {
            error!("{}", e);
        };
    }

    Ok(())
}
