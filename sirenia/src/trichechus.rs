// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A TEE application life-cycle manager.

#![deny(unsafe_op_in_unsafe_fn)]

use std::cell::RefCell;
use std::collections::{BTreeMap as Map, VecDeque};
use std::env;
use std::io::{self, stderr, Seek, SeekFrom, Write};
use std::mem::swap;
use std::ops::{Deref, DerefMut};
use std::os::unix::io::{AsRawFd, RawFd};
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::result::Result as StdResult;

use anyhow::{bail, Context, Result};
use getopts::Options;
use libchromeos::secure_blob::SecureBlob;
use libsirenia::{
    build_info::BUILD_TIMESTAMP,
    cli::{trichechus::initialize_common_arguments, TransportTypeOption},
    communication::{
        persistence::{Cronista, CronistaClient, Status},
        trichechus::{self, AppInfo, SystemEvent, Trichechus, TrichechusServer},
        StorageRpc, StorageRpcServer,
    },
    linux::{
        events::{AddEventSourceMutator, EventMultiplexer, Mutator},
        syslog::{Syslog, SyslogReceiverMut, SYSLOG_PATH},
    },
    rpc::{self, ConnectionHandler, RpcDispatcher, TransportServer},
    sandbox::{MinijailSandbox, Sandbox, VmConfig, VmSandbox},
    sys::{self, halt, power_off, reboot},
    transport::{
        create_transport_from_pipes, Transport, TransportType, CROS_CID, CROS_CONNECTION_ERR_FD,
        CROS_CONNECTION_R_FD, CROS_CONNECTION_W_FD, DEFAULT_CLIENT_PORT, DEFAULT_CONNECTION_R_FD,
        DEFAULT_CONNECTION_W_FD, DEFAULT_CRONISTA_PORT, DEFAULT_SERVER_PORT,
    },
};
use sirenia::{
    app_info::{
        self, AppManifest, AppManifestEntry, Digest, ExecutableInfo, SandboxType, StorageParameters,
    },
    compute_sha256, log_error,
    secrets::{
        self, storage_encryption::StorageEncryption, GscSecret, PlatformSecret, SecretManager,
        VersionedSecret,
    },
};
use sys_util::{
    self, error, getpid, getsid, info, reap_child, setsid, syslog,
    vsock::SocketAddr as VSocketAddr, warn, MemfdSeals, Pid, SharedMemory,
};

const CRONISTA_URI_SHORT_NAME: &str = "C";
const CRONISTA_URI_LONG_NAME: &str = "cronista";
const SYSLOG_PATH_SHORT_NAME: &str = "L";

const CROSVM_PATH: &str = "/bin/crosvm-direct";

/* Holds the trichechus-relevant information for a TEEApp. */
struct TeeApp {
    sandbox: Box<dyn Sandbox>,
    app_info: AppManifestEntry,
}

#[derive(Clone)]
struct TeeAppHandler {
    state: Rc<RefCell<TrichechusState>>,
    tee_app: Rc<RefCell<TeeApp>>,
}

impl TeeAppHandler {
    fn conditionally_use_storage_encryption<
        T: Sized,
        F: FnOnce(
                &StorageParameters,
                &dyn Cronista<Error = rpc::Error>,
            ) -> StdResult<T, rpc::Error>
            + Copy,
    >(
        &self,
        cb: F,
    ) -> StdResult<T, ()> {
        let app_info = &self.tee_app.borrow().app_info;
        let params = app_info.storage_parameters.as_ref().ok_or_else(|| {
            error!(
                "App id '{}' made an unconfigured call to the write_data storage API.",
                &app_info.app_name
            );
        })?;
        let state = self.state.borrow_mut();
        // Holds the RefMut until secret_manager is dropped.
        let wrapper = &mut state.secret_manager.borrow_mut();
        let secret_manager = wrapper.deref_mut();

        // If the operation fails with an rpc::Error, try again.
        for x in 0..=1 {
            // If already connected try once, to see if the connection dropped.
            if let Some(persistence) = (*state.persistence.borrow().deref()).as_ref() {
                let encryption: StorageEncryption;
                let ret = cb(
                    params,
                    match params.encryption_key_version {
                        Some(_) => {
                            // TODO Move this to TrichechusState.
                            encryption =
                                StorageEncryption::new(app_info, secret_manager, persistence);
                            &encryption as &dyn Cronista<Error = rpc::Error>
                        }
                        None => persistence as &dyn Cronista<Error = rpc::Error>,
                    },
                );
                match ret {
                    Err(err) => {
                        // If the client is no longer valid, drop it so it will be recreated on the next call.
                        state.drop_persistence();
                        error!("failed to persist data: {}", err);
                        if x == 1 {
                            break;
                        }
                    }
                    Ok(a) => return Ok(a),
                }
            }

            state.check_persistence().map_err(|err| {
                error!("failed to persist data: {}", err);
            })?;
        }
        Err(())
    }
}

impl StorageRpc for TeeAppHandler {
    type Error = ();

    fn read_data(&self, id: String) -> StdResult<(Status, Vec<u8>), Self::Error> {
        self.conditionally_use_storage_encryption(|params, cronista| {
            cronista.retrieve(params.scope.clone(), params.domain.to_string(), id.clone())
        })
    }

    fn write_data(&self, id: String, data: Vec<u8>) -> StdResult<Status, Self::Error> {
        self.conditionally_use_storage_encryption(|params, cronista| {
            cronista.persist(
                params.scope.clone(),
                params.domain.to_string(),
                id.clone(),
                data.clone(),
            )
        })
    }
}

struct TrichechusState {
    expected_port: u32,
    pending_apps: Map<TransportType, TeeApp>,
    running_apps: Map<Pid, Rc<RefCell<TeeApp>>>,
    log_queue: VecDeque<Vec<u8>>,
    persistence_uri: TransportType,
    persistence: RefCell<Option<CronistaClient>>,
    secret_manager: RefCell<SecretManager>,
    app_manifest: AppManifest,
    loaded_apps: RefCell<Map<Digest, SharedMemory>>,
}

impl TrichechusState {
    fn new(platform_secret: PlatformSecret, gsc_secret: GscSecret) -> Self {
        let app_manifest = AppManifest::load_default().unwrap();
        // There isn't any way to recover if the secret derivation process fails.
        let secret_manager =
            SecretManager::new(platform_secret, gsc_secret, &app_manifest).unwrap();

        TrichechusState {
            expected_port: DEFAULT_CLIENT_PORT,
            pending_apps: Map::new(),
            running_apps: Map::new(),
            log_queue: VecDeque::new(),
            persistence_uri: TransportType::VsockConnection(VSocketAddr {
                cid: CROS_CID,
                port: DEFAULT_CRONISTA_PORT,
            }),
            persistence: RefCell::new(None),
            secret_manager: RefCell::new(secret_manager),
            app_manifest,
            loaded_apps: RefCell::new(Map::new()),
        }
    }

    fn check_persistence(&self) -> Result<()> {
        if self.persistence.borrow().is_some() {
            return Ok(());
        }
        let uri = self.persistence_uri.clone();
        *self.persistence.borrow_mut().deref_mut() = Some(CronistaClient::new(
            uri.try_into_client(None)
                .unwrap()
                .connect()
                .context("failed create transport")?,
        ));
        Ok(())
    }

    fn drop_persistence(&self) {
        *self.persistence.borrow_mut().deref_mut() = None;
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

    fn start_session(&self, app_info: AppInfo) -> StdResult<StdResult<(), trichechus::Error>, ()> {
        info!("Received start session message: {:?}", &app_info);
        // The TEE app isn't started until its socket connection is accepted.
        Ok(log_error(start_session(
            self.state.borrow_mut().deref_mut(),
            self.port_to_transport_type(app_info.port_number),
            &app_info.app_id,
        )))
    }

    fn load_app(
        &self,
        app_id: String,
        elf: Vec<u8>,
    ) -> StdResult<StdResult<(), trichechus::Error>, ()> {
        info!("Received load app message: {:?}", &app_id);
        // The TEE app isn't started until its socket connection is accepted.
        Ok(log_error(load_app(
            self.state.borrow_mut().deref_mut(),
            &app_id,
            &elf,
        )))
    }

    fn get_apps(&self) -> StdResult<Vec<(String, ExecutableInfo)>, ()> {
        info!("Received get apps message");
        Ok(self
            .state
            .borrow()
            .app_manifest
            .iter()
            .map(|e| (e.app_name.clone(), e.exec_info.clone()))
            .collect())
    }

    fn get_logs(&self) -> StdResult<Vec<Vec<u8>>, ()> {
        let mut replacement: VecDeque<Vec<u8>> = VecDeque::new();
        swap(&mut self.state.borrow_mut().log_queue, &mut replacement);
        Ok(replacement.into())
    }

    fn system_event(&self, event: SystemEvent) -> StdResult<StdResult<(), String>, Self::Error> {
        Ok(log_error(system_event(event)))
    }
}

struct DugongConnectionHandler {
    state: Rc<RefCell<TrichechusState>>,
}

impl DugongConnectionHandler {
    fn new(state: Rc<RefCell<TrichechusState>>) -> Self {
        DugongConnectionHandler { state }
    }

    fn connect_tee_app(&mut self, app: TeeApp, connection: Transport) -> Option<Box<dyn Mutator>> {
        let state = self.state.clone();
        // Only borrow once.
        let mut trichechus_state = self.state.borrow_mut();
        match spawn_tee_app(&trichechus_state, app, connection) {
            Ok((pid, app, transport)) => {
                let tee_app = Rc::new(RefCell::new(app));
                trichechus_state.running_apps.insert(pid, tee_app.clone());
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
        if let Some(app) = reservation {
            info!("starting instance of '{}'", &app.app_info.app_name);
            self.connect_tee_app(app, connection)
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

fn fd_to_path(fd: RawFd) -> StdResult<PathBuf, io::Error> {
    PathBuf::from(format!("/proc/self/fd/{}", fd)).read_link()
}

fn lookup_app_info<'a>(
    state: &'a TrichechusState,
    app_id: &str,
) -> StdResult<&'a AppManifestEntry, trichechus::Error> {
    state
        .app_manifest
        .get_app_manifest_entry(app_id)
        .map_err(|err| {
            if let app_info::Error::InvalidAppId(_) = err {
                trichechus::Error::InvalidAppId
            } else {
                trichechus::Error::from(format!("Failed to get manifest entry: {}", err))
            }
        })
}

fn load_app(state: &TrichechusState, app_id: &str, elf: &[u8]) -> StdResult<(), trichechus::Error> {
    let app_info = lookup_app_info(state, app_id)?;

    // Validate digest.
    let expected = match &app_info.exec_info {
        ExecutableInfo::Digest(expected) => expected,
        ExecutableInfo::CrosPath(_, Some(expected)) => expected,
        _ => {
            return Err(trichechus::Error::AppNotLoadable);
        }
    };
    let actual = compute_sha256(elf)
        .map_err(|err| trichechus::Error::from(format!("SHA256 failed: {:?}", err)))?;
    if expected.deref() != &*actual {
        return Err(trichechus::Error::DigestMismatch);
    }

    // Create and write memfd.
    let mut executable = SharedMemory::new(None)
        .map_err(|err| trichechus::Error::from(format!("Failed to open memfd: {:?}", err)))?;
    executable
        .write_all(elf)
        .map_err(|err| trichechus::Error::from(format!("Failed to write to memfd: {:?}", err)))?;
    executable
        .seek(SeekFrom::Start(0))
        .map_err(|err| trichechus::Error::from(format!("Failed to seek memfd: {:?}", err)))?;

    // Seal memfd.
    let mut seals = MemfdSeals::default();
    seals.set_grow_seal();
    seals.set_shrink_seal();
    seals.set_write_seal();
    executable
        .add_seals(seals)
        .map_err(|err| trichechus::Error::from(format!("Failed to seal memfd: {:?}", err)))?;

    state.loaded_apps.borrow_mut().insert(actual, executable);
    Ok(())
}

fn start_session(
    state: &mut TrichechusState,
    key: TransportType,
    app_id: &str,
) -> StdResult<(), trichechus::Error> {
    let app_info = lookup_app_info(state, app_id)?.to_owned();

    let sandbox: Box<dyn Sandbox> = match &app_info.sandbox_type {
        SandboxType::DeveloperEnvironment => {
            Box::new(MinijailSandbox::passthrough().map_err(|err| {
                trichechus::Error::from(format!("Failed to create sandbox: {:?}", err))
            })?)
        }
        SandboxType::Container => Box::new(MinijailSandbox::new(None).map_err(|err| {
            trichechus::Error::from(format!("Failed to create sandbox: {:?}", err))
        })?),
        SandboxType::VirtualMachine => Box::new(
            VmSandbox::new(VmConfig {
                crosvm_path: PathBuf::from(CROSVM_PATH),
            })
            .map_err(|err| {
                trichechus::Error::from(format!("Failed to create sandbox: {:?}", err))
            })?,
        ),
    };

    // Do some additional checks to fail early and return the reason to the caller.
    match &app_info.exec_info {
        ExecutableInfo::Path(path) => {
            if !Path::new(&path).is_file() {
                return Err(trichechus::Error::AppPath);
            }
        }
        ExecutableInfo::CrosPath(_, None) => {
            return Err(trichechus::Error::DigestMissing);
        }
        ExecutableInfo::Digest(digest) | ExecutableInfo::CrosPath(_, Some(digest)) => {
            if state.loaded_apps.borrow().get(digest).is_none() {
                return Err(trichechus::Error::AppNotLoaded);
            }
        }
    }

    state.pending_apps.insert(key, TeeApp { sandbox, app_info });
    Ok(())
}

fn spawn_tee_app(
    state: &TrichechusState,
    mut app: TeeApp,
    transport: Transport,
) -> Result<(Pid, TeeApp, Transport)> {
    let (trichechus_transport, tee_transport) =
        create_transport_from_pipes().context("failed create transport")?;
    let keep_fds: [(RawFd, RawFd); 5] = [
        (transport.r.as_raw_fd(), CROS_CONNECTION_R_FD),
        (transport.w.as_raw_fd(), CROS_CONNECTION_W_FD),
        (stderr().as_raw_fd(), CROS_CONNECTION_ERR_FD),
        (tee_transport.r.as_raw_fd(), DEFAULT_CONNECTION_R_FD),
        (tee_transport.w.as_raw_fd(), DEFAULT_CONNECTION_W_FD),
    ];

    let exe_args = app
        .app_info
        .exec_args
        .as_deref()
        .unwrap_or(&[])
        .iter()
        .map(|a| a.as_str());
    let pid = match &app.app_info.exec_info {
        ExecutableInfo::Path(path) => {
            let mut args = Vec::with_capacity(1 + exe_args.len());
            args.push(path.as_str());
            args.extend(exe_args);
            app.sandbox
                .run(Path::new(&path), args.as_slice(), &keep_fds)
                .context("failed to start up sandbox")?
        }
        ExecutableInfo::CrosPath(_, None) => {
            bail!(
                "digest missing for TEE app {}",
                app.app_info.app_name.clone()
            );
        }
        ExecutableInfo::Digest(digest) | ExecutableInfo::CrosPath(_, Some(digest)) => {
            match state.loaded_apps.borrow().get(digest) {
                Some(shared_mem) => {
                    let fd_path = fd_to_path(shared_mem.as_raw_fd())
                        .map(|p| p.to_string_lossy().to_string())
                        .unwrap_or_else(|_| "".into());
                    let mut args = Vec::with_capacity(1 + exe_args.len());
                    args.push(fd_path.as_str());
                    args.extend(exe_args);
                    app.sandbox
                        .run_raw(shared_mem.as_raw_fd(), args.as_slice(), &keep_fds)
                        .context("failed to start up sandbox")?
                }
                None => bail!(
                    "error retrieving path for TEE app: {0}",
                    app.app_info.app_name.clone()
                ),
            }
        }
    };

    Ok((pid, app, trichechus_transport))
}

fn system_event(event: SystemEvent) -> StdResult<(), String> {
    match event {
        SystemEvent::Halt => halt(),
        SystemEvent::PowerOff => power_off(),
        SystemEvent::Reboot => reboot(),
    }
    .map_err(|err| format!("{}", err))
}

fn handle_closed_child_processes(state: &RefCell<TrichechusState>) {
    loop {
        match reap_child() {
            Ok(0) => {
                break;
            }
            Ok(pid) => {
                if let Some(app) = state.borrow_mut().running_apps.remove(&pid) {
                    info!("Instance of '{}' exited.", &app.borrow().app_info.app_name);
                } else {
                    warn!("Untracked process exited '{}'.", pid);
                }
            }
            Err(err) => {
                if err.errno() == libc::ECHILD {
                    break;
                } else {
                    error!("Waitpid exited with: {}", err);
                }
            }
        }
    }
}

// TODO: Figure out rate limiting and prevention against DOS attacks
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
    // TODO derive main secret from the platform and GSC.
    let main_secret_version = 0usize;
    let platform_secret = PlatformSecret::new(
        SecretManager::default_hash_function(),
        SecureBlob::from(vec![77u8; 64]),
        secrets::MAX_VERSION,
    )
    .derive_other_version(main_secret_version)
    .unwrap();
    let gsc_secret = GscSecret::new(
        SecretManager::default_hash_function(),
        SecureBlob::from(vec![77u8; 64]),
        secrets::MAX_VERSION,
    )
    .derive_other_version(main_secret_version)
    .unwrap();
    let state = Rc::new(RefCell::new(TrichechusState::new(
        platform_secret,
        gsc_secret,
    )));

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
        bail!("failed to initialize the syslog: {}", e);
    }
    info!("starting trichechus: {}", BUILD_TIMESTAMP);

    if getpid() != getsid(None).unwrap() {
        if let Err(err) = setsid() {
            error!("Unable to start new process group: {}", err);
        }
    }
    sys::block_all_signals();
    // This is safe because no additional file descriptors have been opened (except syslog which
    // cannot be dropped until we are ready to clean up /dev/log).
    let ret = unsafe { sys::fork() }.unwrap();
    if ret != 0 {
        // The parent process collects the return codes from the child processes, so they do not
        // remain zombies.
        while sys::wait_for_child() {}
        info!("reaper done!");
        return Ok(());
    }

    // Unblock signals for the process that spawns the children. It might make sense to fork
    // again here for each child to avoid them blocking each other.
    sys::unblock_all_signals();

    if let Some(uri) = cronista_uri_option.from_matches(&matches).unwrap() {
        let mut state_mut = state.borrow_mut();
        state_mut.persistence_uri = uri.clone();
        *state_mut.persistence.borrow_mut().deref_mut() = Some(CronistaClient::new(
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
        handle_closed_child_processes(state.deref());
    }

    Ok(())
}
