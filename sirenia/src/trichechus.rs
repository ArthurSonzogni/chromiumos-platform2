// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A TEE application life-cycle manager.

#![allow(clippy::type_complexity)]
#![deny(unsafe_op_in_unsafe_fn)]

use std::cell::RefCell;
use std::cmp::min;
use std::collections::BTreeMap as Map;
use std::collections::BTreeSet;
use std::collections::VecDeque;
use std::convert::TryFrom;
use std::env;
use std::ffi::CString;
use std::fs::remove_file;
use std::fs::File;
use std::io::BufWriter;
use std::io::IoSlice;
use std::io::Seek;
use std::io::SeekFrom;
use std::io::Write;
use std::mem::replace;
use std::mem::swap;
use std::mem::take;
use std::ops::Deref;
use std::ops::DerefMut;
use std::os::raw::c_int;
use std::os::unix::io::AsRawFd;
use std::os::unix::io::RawFd;
use std::path::Path;
use std::path::PathBuf;
use std::rc::Rc;
use std::result::Result as StdResult;
use std::str::FromStr;
use std::time::Duration;
use std::time::Instant;

use anyhow::anyhow;
use anyhow::bail;
use anyhow::Context;
use anyhow::Error;
use anyhow::Result;
use crosvm_base::net::UnixSeqpacket;
use crosvm_base::net::UnixSeqpacketListener;
use crosvm_base::unix::getpid;
use crosvm_base::unix::getsid;
use crosvm_base::unix::pipe;
use crosvm_base::unix::reap_child;
use crosvm_base::unix::setsid;
use crosvm_base::unix::vsock::SocketAddr as VSocketAddr;
use crosvm_base::unix::MemfdSeals;
use crosvm_base::unix::Pid;
use crosvm_base::unix::ScmSocket;
use crosvm_base::unix::SharedMemory;
use crosvm_base::AsRawDescriptor;
use getopts::Options;
use libchromeos::chromeos::is_dev_mode;
use libchromeos::secure_blob::SecureBlob;
use libchromeos::syslog;
use libsirenia::app_info;
use libsirenia::app_info::AppManifest;
use libsirenia::app_info::AppManifestEntry;
use libsirenia::app_info::Digest;
use libsirenia::app_info::ExecutableInfo;
use libsirenia::app_info::SandboxType;
use libsirenia::app_info::StdErrBehavior;
use libsirenia::app_info::StorageParameters;
use libsirenia::build_info::BUILD_TIMESTAMP;
use libsirenia::cli::trichechus::initialize_common_arguments;
use libsirenia::cli::TransportTypeOption;
use libsirenia::communication::persistence::Cronista;
use libsirenia::communication::persistence::CronistaClient;
use libsirenia::communication::persistence::Status;
use libsirenia::communication::tee_api::TeeApi;
use libsirenia::communication::tee_api::TeeApiServer;
use libsirenia::communication::trichechus;
use libsirenia::communication::trichechus::AppInfo;
use libsirenia::communication::trichechus::SystemEvent;
use libsirenia::communication::trichechus::Trichechus;
use libsirenia::communication::trichechus::TrichechusServer;
use libsirenia::linux::events::AddEventSourceMutator;
use libsirenia::linux::events::ComboMutator;
use libsirenia::linux::events::CopyFdEventSource;
use libsirenia::linux::events::EventMultiplexer;
use libsirenia::linux::events::LogFromFdEventSource;
use libsirenia::linux::events::Mutator;
use libsirenia::linux::kmsg;
use libsirenia::linux::kmsg::SyslogForwarderMut;
use libsirenia::linux::kmsg::KMSG_PATH;
use libsirenia::linux::syslog::Syslog;
use libsirenia::linux::syslog::SyslogReceiverMut;
use libsirenia::linux::syslog::SYSLOG_PATH;
use libsirenia::rpc::ConnectionHandler;
use libsirenia::rpc::RpcDispatcher;
use libsirenia::rpc::TransportServer;
use libsirenia::sandbox::MinijailSandbox;
use libsirenia::sandbox::Sandbox;
use libsirenia::sandbox::VmConfig;
use libsirenia::sandbox::VmSandbox;
use libsirenia::secrets;
use libsirenia::secrets::compute_sha256;
use libsirenia::secrets::storage_encryption::StorageEncryption;
use libsirenia::secrets::GscSecret;
use libsirenia::secrets::PlatformSecret;
use libsirenia::secrets::SecretManager;
use libsirenia::secrets::VersionedSecret;
use libsirenia::sys;
use libsirenia::sys::dev_null;
use libsirenia::sys::dup;
use libsirenia::sys::get_a_pty;
use libsirenia::sys::halt;
use libsirenia::sys::power_off;
use libsirenia::sys::reboot;
use libsirenia::transport::create_transport_from_pipes;
use libsirenia::transport::Transport;
use libsirenia::transport::TransportRead;
use libsirenia::transport::TransportType;
use libsirenia::transport::TransportWrite;
use libsirenia::transport::CROS_CID;
use libsirenia::transport::CROS_CONNECTION_ERR_FD;
use libsirenia::transport::CROS_CONNECTION_R_FD;
use libsirenia::transport::CROS_CONNECTION_W_FD;
use libsirenia::transport::DEFAULT_CLIENT_PORT;
use libsirenia::transport::DEFAULT_CONNECTION_R_FD;
use libsirenia::transport::DEFAULT_CONNECTION_W_FD;
use libsirenia::transport::DEFAULT_CRONISTA_PORT;
use libsirenia::transport::DEFAULT_SERVER_PORT;
use log::error;
use log::info;
use log::warn;
use sirenia::log_error;
use sirenia::pstore;

const IDENT: &str = "trichechus";
const CRONISTA_URI_SHORT_NAME: &str = "C";
const CRONISTA_URI_LONG_NAME: &str = "cronista";
const SYSLOG_PATH_SHORT_NAME: &str = "L";
const PSTORE_PATH_LONG_NAME: &str = "pstore-path";
const SAVE_PSTORE_LONG_NAME: &str = "save-pstore";
const RESTORE_PSTORE_LONG_NAME: &str = "restore-pstore";
const SAVE_HYPERVISOR_DMESG: &str = "save-hypervisor-dmesg";
const MMS_BRIDGE_SHORT_NAME: &str = "M";
const LOG_TO_STDERR_LONG_NAME: &str = "log-to-stderr";

const CROSVM_PATH: &str = "/bin/crosvm-direct";

const APP_START_TIMEOUT: Duration = Duration::from_secs(30);

enum InstanceState {
    Pending {
        // Expected incoming connections transition to established connections until
        // pending_connections is empty and then the TEE app can be started.
        pending_connections: Vec<(TransportType, c_int)>,
        established_connections: Vec<(Transport, c_int)>,
        expiration: Instant,
    },
    Running,
}

/* Holds the trichechus-relevant information for a TEEApp. */
struct TeeAppInstance {
    sandbox: Box<dyn Sandbox>,
    app_info: AppManifestEntry,
    args: Vec<String>,

    // Keep track of state specific information like expected connections.
    state: InstanceState,
}

#[derive(Clone)]
struct TeeAppHandler {
    state: Rc<RefCell<TrichechusState>>,
    tee_app: Rc<RefCell<TeeAppInstance>>,
}

impl TeeAppHandler {
    fn conditionally_use_storage_encryption<
        T: Sized,
        F: FnOnce(&StorageParameters, &mut dyn Cronista<anyhow::Error>) -> Result<T> + Copy,
    >(
        &self,
        cb: F,
    ) -> Result<T> {
        let app_info = &self.tee_app.borrow().app_info;
        let params = app_info.storage_parameters.as_ref().ok_or_else(|| {
            let msg = format!(
                "App id '{}' made an unconfigured call to the write_data storage API.",
                &app_info.app_name
            );
            error!("{}", &msg);
            anyhow!(msg)
        })?;
        let state = self.state.borrow_mut();
        // Holds the RefMut until secret_manager is dropped.
        let wrapper = &mut state.secret_manager.borrow_mut();
        let secret_manager = wrapper.deref_mut();

        // If the operation fails with an rpc::Error, try again.
        for x in 0..=1 {
            // If already connected try once, to see if the connection dropped.
            if let Some(persistence) = (*state.persistence.borrow_mut().deref_mut()).as_mut() {
                let mut encryption: StorageEncryption;
                let ret = cb(
                    params,
                    match params.encryption_key_version {
                        Some(_) => {
                            // TODO Move this to TrichechusState.
                            encryption =
                                StorageEncryption::new(app_info, secret_manager, persistence);
                            &mut encryption as &mut dyn Cronista<anyhow::Error>
                        }
                        None => persistence as &mut dyn Cronista<anyhow::Error>,
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
                let msg: &str = "failed to persist data";
                error!("{}: {}", msg, err);
                err.context(msg)
            })?;
        }
        Err(anyhow!(""))
    }
}

impl TeeApi<Error> for TeeAppHandler {
    fn read_data(&mut self, id: String) -> Result<(Status, Vec<u8>)> {
        self.conditionally_use_storage_encryption(|params, cronista| {
            cronista.retrieve(params.scope.clone(), params.domain.to_string(), id.clone())
        })
    }

    fn remove(&mut self, id: String) -> StdResult<Status, Error> {
        self.conditionally_use_storage_encryption(|params, cronista| {
            cronista.remove(params.scope.clone(), params.domain.to_string(), id.clone())
        })
    }

    fn write_data(&mut self, id: String, data: Vec<u8>) -> Result<Status> {
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
    pending_apps: Map<TransportType, Rc<RefCell<TeeAppInstance>>>,
    running_apps: Map<Pid, Rc<RefCell<TeeAppInstance>>>,
    log_queue: VecDeque<Vec<u8>>,
    persistence_uri: TransportType,
    persistence: RefCell<Option<CronistaClient>>,
    secret_manager: RefCell<SecretManager>,
    app_manifest: AppManifest,
    loaded_apps: RefCell<Map<Digest, SharedMemory>>,
    mms_bridge: Option<UnixSeqpacket>,
    pending_mms_port: Option<u32>,
    kmsg: RefCell<Option<BufWriter<File>>>,
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
            mms_bridge: None,
            pending_mms_port: None,
            kmsg: RefCell::new(match File::options().write(true).open(KMSG_PATH) {
                Err(e) => {
                    eprintln!(
                        "Unable to open /dev/kmsg for writing. \
                               Syslog messages will be missing from kernel \
                               crash reports: {}",
                        e
                    );
                    None
                }
                Ok(f) => Some(BufWriter::new(f)),
            }),
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

    // Check for expired entries and return the next expiration time.
    fn cleanup_expired(&mut self) -> Option<Instant> {
        let now = Instant::now();
        let mut next_expiration = None;
        self.pending_apps.retain(|_, v| {
            if let InstanceState::Pending {
                pending_connections: _,
                established_connections: _,
                expiration,
            } = v.borrow().state
            {
                if expiration < now {
                    false
                } else {
                    next_expiration = Some(min(next_expiration.unwrap_or(expiration), expiration));
                    true
                }
            } else {
                false
            }
        });
        next_expiration
    }
}

// Parse raw message written to syslog socket and get severity and message.
// Returns severity of "Invalid" and the raw message on parse failure.
fn get_severity_and_msg(s: &str) -> (&str, &str) {
    let mut sev: u8 = 255;
    let mut msg: &str = s;
    if msg.starts_with('<') {
        if let Some(gtpos) = msg.find('>') {
            if let Ok(i) = u8::from_str(&s[1..gtpos]) {
                sev = i & 7;
                msg = &s[gtpos + 1..];
            }
        }
    }
    (
        match sev {
            0 => "Emergency",
            1 => "Alert",
            2 => "Critical",
            3 => "Error",
            4 => "Warning",
            5 => "Notice",
            6 => "Info",
            7 => "Debug",
            255 => "Invalid",
            _ => "Unknown",
        },
        msg,
    )
}

impl SyslogReceiverMut for TrichechusState {
    // Before forwarding syslog messages to the Chrome OS rsyslog daemon, write
    // them to /dev/kmsg so that (1) they appear on the console, and (2) they
    // are included in kernel panic crash reports.
    fn receive(&mut self, data: Vec<u8>) {
        if let Some(kmsgf) = self.kmsg.borrow_mut().deref_mut() {
            let rawmsg = String::from_utf8_lossy(&data);
            let (sev, msg) = get_severity_and_msg(&rawmsg);
            let r = writeln!(kmsgf, "syslog: [{}] {}", sev, kmsg::escape(msg))
                .and_then(|_| kmsgf.flush());
            if let Err(e) = r {
                eprintln!("syslog: {}", kmsg::escape(&rawmsg));
                eprintln!("Can't write to /dev/kmsg: {}", e);
            }
        }
        self.log_queue.push_back(data);
    }
}

impl SyslogForwarderMut for TrichechusState {
    fn forward(&mut self, data: Vec<u8>) {
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

impl Trichechus<Error> for TrichechusServerImpl {
    fn start_session(&mut self, app_info: AppInfo, args: Vec<String>) -> Result<u32> {
        info!("Received start session message: {:?}", &app_info);
        // The TEE app isn't started until its socket connection is accepted.

        // Validate the port numbers.
        let deduplicated_ports: BTreeSet<u32> = app_info.port_numbers.iter().cloned().collect();
        if deduplicated_ports.len() != app_info.port_numbers.len() {
            return log_error(
                Err(trichechus::Error::DuplicateSourcePort).with_context(|| {
                    format!(
                        "start_session for app '{}' had duplicate port_numbers",
                        app_info.app_id.as_str()
                    )
                }),
            );
        }

        Ok(log_error(start_session(
            self.state.borrow_mut().deref_mut(),
            app_info
                .port_numbers
                .iter()
                .map(|p| self.port_to_transport_type(*p))
                .collect(),
            &app_info.app_id,
            args,
        ))?)
    }

    fn load_app(&mut self, app_id: String, elf: Vec<u8>) -> Result<()> {
        info!("Received load app message: {:?}", &app_id);
        // The TEE app isn't started until its socket connection is accepted.
        Ok(log_error(load_app(
            self.state.borrow_mut().deref_mut(),
            &app_id,
            &elf,
        ))?)
    }

    fn get_apps(&mut self) -> Result<AppManifest> {
        info!("Received get apps message");
        Ok(self.state.borrow().app_manifest.clone())
    }

    fn get_logs(&mut self) -> Result<Vec<Vec<u8>>> {
        let mut replacement: VecDeque<Vec<u8>> = VecDeque::new();
        swap(&mut self.state.borrow_mut().log_queue, &mut replacement);
        Ok(replacement.into())
    }

    fn prepare_manatee_memory_service_socket(&mut self, port_number: u32) -> Result<()> {
        if self.state.borrow().mms_bridge.is_none() {
            bail!("No mms_bridge");
        }
        self.state.borrow_mut().pending_mms_port = Some(port_number);
        Ok(())
    }

    fn system_event(&mut self, event: SystemEvent) -> Result<()> {
        log_error(system_event(event)).map_err(|err| trichechus::Error::from(err).into())
    }
}

struct DugongConnectionHandler {
    state: Rc<RefCell<TrichechusState>>,
}

fn get_stdio_indices(entries: &[(Transport, c_int)]) -> Option<(usize, usize)> {
    let mut stdin = None;
    let mut stdout = None;
    for (i, entry) in entries.iter().enumerate() {
        match entry.1 {
            CROS_CONNECTION_R_FD => {
                stdin = Some(i);
            }
            CROS_CONNECTION_W_FD => {
                stdout = Some(i);
            }
            _ => {}
        }
    }
    if let Some(stdin) = stdin {
        if let Some(stdout) = stdout {
            return Some((stdin, stdout));
        }
    }
    None
}

fn setup_pty(connections: &mut Vec<(Transport, i32)>) -> Result<[Box<dyn Mutator>; 4]> {
    let (stdin, stdout) =
        get_stdio_indices(connections).ok_or_else(|| anyhow!("failed to identify stdio"))?;
    let (main, client) = get_a_pty().context("failed get a pty")?;

    let dup_main: File = dup(main.as_raw_fd()).context("failed dup pty main")?;
    let dup_client: File = dup(client.as_raw_fd()).context("failed dup pty client")?;
    let dev_null = dev_null().context("failed to open /dev/null")?;

    let Transport { r, w: _, id: _ } = replace(
        &mut connections[stdin].0,
        Transport::from_files(
            client,
            dev_null.try_clone().context("Failed to clone /dev/null")?,
        ),
    );
    let Transport { r: _, w, id: _ } = replace(
        &mut connections[stdout].0,
        Transport::from_files(dev_null, dup_client),
    );

    let main_r: Box<dyn TransportRead> = Box::new(main);
    let main_w: Box<dyn TransportWrite> = Box::new(dup_main);
    let w_copy = CopyFdEventSource::new(r, main_w).context("failed to setup pty pipe")?;
    let r_copy = CopyFdEventSource::new(main_r, w).context("failed to setup pty pipe")?;
    Ok([
        Box::new(AddEventSourceMutator::from(w_copy.0)),
        Box::new(AddEventSourceMutator::from(w_copy.1)),
        Box::new(AddEventSourceMutator::from(r_copy.0)),
        Box::new(AddEventSourceMutator::from(r_copy.1)),
    ])
}

impl DugongConnectionHandler {
    fn new(state: Rc<RefCell<TrichechusState>>) -> Self {
        DugongConnectionHandler { state }
    }

    /// A helper function called by Self::handle_incoming_connection when the incoming connection is
    /// found to be associated with a specific TEE app instance. The association is defined by the
    /// source port numbers listed in a start_session call from the same source address as the
    /// control connection.
    ///
    /// Since multiple ports may be listened, multiple connections may need to be established and
    /// associated with a TEE app instance before executing the TEE app.
    fn connect_tee_app(
        &mut self,
        app_ref: Rc<RefCell<TeeAppInstance>>,
        connection: Transport,
    ) -> Option<Box<dyn Mutator>> {
        let mut app_ref_mut = app_ref.borrow_mut();
        let app = app_ref_mut.deref_mut();
        let app_name = app.app_info.app_id();
        let mut established_connections = match &mut app.state {
            InstanceState::Pending {
                pending_connections,
                established_connections,
                expiration: _,
            } => {
                let mut fd = None;
                pending_connections.retain(|p| {
                    // Match the incoming connection against the expected list of connections and
                    // set the corresponding fd number. An earlier check prevents the same source
                    // port from being specified more than once.
                    if p.0 == connection.id {
                        if fd.is_some() {
                            error!("TEE app '{}' has duplicate pending source port", app_name)
                        }
                        fd = Some(p.1);
                        false
                    } else {
                        true
                    }
                });
                let fd = match fd {
                    Some(v) => v,
                    None => {
                        error!("missing pending connection: {}", app_name);
                        return None;
                    }
                };
                established_connections.push((connection, fd));
                if !pending_connections.is_empty() {
                    // Wait for the remaining connections before continuing.
                    return None;
                }
                take(established_connections)
            }
            InstanceState::Running => {
                error!("app in an unexpected state: {}", app_name);
                return None;
            }
        };

        let state = self.state.clone();
        // Only borrow once.
        let mut trichechus_state = self.state.borrow_mut();

        let mut mutators: Vec<Box<dyn Mutator>> = Vec::new();
        if app.app_info.app_name == "shell" {
            match setup_pty(&mut established_connections) {
                Ok(m) => mutators.extend(m),
                Err(err) => {
                    error!("failed to set up pty: {}", err);
                    return None;
                }
            }
        }
        info!("starting instance of '{}'", app_name);

        let (add_event, log_forwarder): (Box<dyn Mutator>, Option<Box<dyn Mutator>>) =
            match spawn_tee_app(&trichechus_state, app, established_connections) {
                Ok((pid, transport, log_forwarder)) => {
                    trichechus_state.running_apps.insert(pid, app_ref.clone());
                    let storage_server: Box<dyn TeeApiServer> = Box::new(TeeAppHandler {
                        state,
                        tee_app: app_ref.clone(),
                    });
                    (
                        RpcDispatcher::new_as_boxed_mutator(storage_server, transport)?,
                        log_forwarder,
                    )
                }
                Err(e) => {
                    error!("failed to start tee app: {}", e);
                    return None;
                }
            };
        app.state = InstanceState::Running;

        if let Some(m) = log_forwarder {
            mutators.push(m);
        }

        if mutators.is_empty() {
            Some(add_event)
        } else {
            mutators.push(add_event);
            Some(Box::new(ComboMutator::from(mutators.into_iter())))
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
            info!(
                "associating connection with '{}'",
                &app.borrow().app_info.app_name
            );
            self.connect_tee_app(app, connection)
        } else {
            // Check if it is a control connection.
            match connection.id.get_port() {
                Ok(port) if port == expected_port => {
                    info!("new control connection.");
                    RpcDispatcher::new_as_boxed_mutator(
                        TrichechusServerImpl::new(self.state.clone(), connection.id.clone())
                            .box_clone(),
                        connection,
                    )
                }
                Ok(port) if Some(port) == self.state.borrow().pending_mms_port => {
                    info!("got manatee memory service connection");
                    self.state.borrow_mut().pending_mms_port.take();
                    let data = vec![0];
                    if let Some(bridge) = self.state.borrow().mms_bridge.as_ref() {
                        if bridge
                            .send_with_fd(&[IoSlice::new(&data)], connection.as_raw_fd())
                            .is_err()
                        {
                            error!("failed to forward connection over bridge");
                        }
                    } else {
                        error!("pending mms port set without bridge");
                    }
                    None
                }
                _ => {
                    error!("dropping unexpected connection.");
                    None
                }
            }
        }
    }
}

fn fd_to_path(fd: RawFd) -> String {
    format!("/proc/{}/fd/{}", getpid(), fd)
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
    // unwrap used because it should always be possible to represent app_id as a CString.
    let mut executable = SharedMemory::new(&CString::new(app_id).unwrap(), elf.len() as u64)
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

    state.loaded_apps.borrow_mut().insert(
        Digest::try_from(actual.as_ref()).expect("Digest size mismatch"),
        executable,
    );
    Ok(())
}

fn start_session(
    state: &mut TrichechusState,
    connections: Vec<TransportType>,
    app_id: &str,
    args: Vec<String>,
) -> StdResult<u32, trichechus::Error> {
    let app_info = lookup_app_info(state, app_id)?.to_owned();
    let fds: Vec<i32> = app_info.channel_config.deref().clone();

    if app_info.devmode_only && !is_dev_mode().unwrap_or(false) {
        return Err(trichechus::Error::RequiresDevmode);
    }

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

    let app = Rc::new(RefCell::new(TeeAppInstance {
        sandbox,
        app_info,
        args,
        state: InstanceState::Pending {
            pending_connections: connections
                .iter()
                .zip(fds.iter())
                .map(|t| (t.0.to_owned(), t.1.to_owned()))
                .collect(),
            established_connections: Vec::new(),
            expiration: Instant::now() + APP_START_TIMEOUT,
        },
    }));
    for key in connections {
        state.pending_apps.insert(key, app.clone());
    }
    Ok(fds.len() as u32)
}

fn spawn_tee_app(
    state: &TrichechusState,
    app: &mut TeeAppInstance,
    transports: Vec<(Transport, i32)>,
) -> Result<(Pid, Transport, Option<Box<dyn Mutator>>)> {
    let (trichechus_transport, tee_transport) =
        create_transport_from_pipes().context("failed create transport")?;

    let mut has_stderr = false;
    let mut keep_fds: Vec<(RawFd, RawFd)> = vec![
        (tee_transport.r.as_raw_fd(), DEFAULT_CONNECTION_R_FD),
        (tee_transport.w.as_raw_fd(), DEFAULT_CONNECTION_W_FD),
    ];
    let mut stdout = None;
    keep_fds.extend(transports.iter().map(|t| {
        if t.1 == CROS_CONNECTION_W_FD {
            stdout = Some(t.0.as_raw_fd());
            // Handle the DupStdio case where the transport for stdin is split in two.
            (t.0.w.as_raw_fd(), CROS_CONNECTION_W_FD)
        } else if t.1 == CROS_CONNECTION_ERR_FD {
            has_stderr = true;
            (t.0.w.as_raw_fd(), CROS_CONNECTION_ERR_FD)
        } else {
            (t.0.as_raw_fd(), t.1)
        }
    }));

    let mut mutator: Option<Box<dyn Mutator>> = None;
    let mut stderr_w: Option<File> = None;
    let mut stderr_behavior = app.app_info.stderr_behavior.clone();

    // Convert StdErrBehavior::Default to the appropriate replacement.
    if stderr_behavior == StdErrBehavior::Default {
        stderr_behavior = if has_stderr || !is_dev_mode().unwrap_or(false) {
            StdErrBehavior::Drop
        } else {
            StdErrBehavior::Syslog
        };
    }

    match stderr_behavior {
        StdErrBehavior::Drop => {
            // Minijail binds stdio to /dev/null if they aren't specified.
        }
        StdErrBehavior::MergeWithStdout => {
            if let Some(w) = stdout {
                keep_fds.push((w, CROS_CONNECTION_ERR_FD));
            } else {
                error!("No stdout found to merge: {}", &app.app_info.app_name);
            }
        }
        StdErrBehavior::Syslog => {
            let (r, w) = pipe(true).context("Failed to create stderr pipe.")?;
            mutator = Some(Box::new(AddEventSourceMutator::from(
                LogFromFdEventSource::new(app.app_info.app_name.clone(), Box::new(r))?,
            )));
            // TODO forward this to syslog instead of trichechus stderr.
            keep_fds.push((w.as_raw_fd(), CROS_CONNECTION_ERR_FD));
            // stderr_w tracks the ownership of the write file descriptor and needs to be open long
            // enough to pass it to the child process. It is explicitly dropped after it is no
            // longer needed.
            stderr_w = Some(w);
        }
        StdErrBehavior::Default => {
            unreachable!("Default should have been replaced above.")
        }
    }

    let exe_args = app
        .app_info
        .exec_args
        .as_deref()
        .unwrap_or(&[])
        .iter()
        .map(|a| a.as_str());
    let pid = match &app.app_info.exec_info {
        ExecutableInfo::Path(path) => {
            let mut args = Vec::with_capacity(1 + exe_args.len() + app.args.len());
            args.push(path.as_str());
            args.extend(exe_args);
            args.extend(app.args.iter().map(AsRef::<str>::as_ref));
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
                    let fd_path = fd_to_path(shared_mem.as_raw_descriptor());
                    let mut args = Vec::with_capacity(1 + exe_args.len() + app.args.len());
                    args.push(fd_path.as_str());
                    args.extend(exe_args);
                    args.extend(app.args.iter().map(AsRef::<str>::as_ref));
                    app.sandbox
                        .run_raw(shared_mem.as_raw_descriptor(), args.as_slice(), &keep_fds)
                        .context("failed to start up sandbox")?
                }
                None => bail!(
                    "error retrieving path for TEE app: {0}",
                    app.app_info.app_name.clone()
                ),
            }
        }
    };
    // This is explicit since stderr_w is not read from, and dropping stderr_w closes the underlying
    // file which needs to live until it has been passed to the child process.
    drop(stderr_w);

    Ok((pid, trichechus_transport, mutator))
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
    opts.optopt(
        MMS_BRIDGE_SHORT_NAME,
        "mms-bridge",
        "socket to provide guest client connections to MMS,",
        "/run/mms-bridge",
    );
    let cronista_uri_option = TransportTypeOption::new(
        CRONISTA_URI_SHORT_NAME,
        CRONISTA_URI_LONG_NAME,
        "URI to connect to cronista",
        "vsock://3:5554",
        &mut opts,
    );
    opts.optopt(
        "",
        PSTORE_PATH_LONG_NAME,
        "path to crosvm pstore file.",
        "/run/crosvm.pstore",
    );
    opts.optflag(
        "",
        SAVE_PSTORE_LONG_NAME,
        "copy pstore file contents to ramoops memory, then exit.",
    );
    opts.optflag(
        "",
        RESTORE_PSTORE_LONG_NAME,
        "copy ramoops memory to pstore file, then exit.",
    );
    opts.optflag(
        "",
        LOG_TO_STDERR_LONG_NAME,
        "write log messages to stderr in addition to syslog.",
    );
    opts.optflag(
        "",
        SAVE_HYPERVISOR_DMESG,
        "add hypervisor dmesg to pstore console log.",
    );
    let (config, matches) = initialize_common_arguments(opts, &args[1..]).unwrap();
    let log_to_stderr = matches.opt_present(LOG_TO_STDERR_LONG_NAME);

    if let Some(pstore_path) = matches.opt_str(PSTORE_PATH_LONG_NAME) {
        syslog::init(IDENT.to_string(), log_to_stderr)
            .map_err(|e| anyhow!("failed to setup logging: {}", e))?;
        if matches.opt_present(SAVE_PSTORE_LONG_NAME) {
            return pstore::save_pstore(&pstore_path, matches.opt_present(SAVE_HYPERVISOR_DMESG));
        } else if matches.opt_present(RESTORE_PSTORE_LONG_NAME) {
            return pstore::restore_pstore(&pstore_path);
        } else {
            bail!("pstore path given but no action selected");
        }
    }

    if matches.opt_present(SAVE_PSTORE_LONG_NAME)
        || matches.opt_present(RESTORE_PSTORE_LONG_NAME)
        || matches.opt_present(SAVE_HYPERVISOR_DMESG)
    {
        bail!("{} is required for pstore actions", PSTORE_PATH_LONG_NAME);
    }

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

    syslog::init(IDENT.to_string(), log_to_stderr)
        .map_err(|e| anyhow!("failed to setup logging: {}", e))?;
    info!("starting {}: {}", IDENT, BUILD_TIMESTAMP);

    if let Some(path) = matches.opt_str(MMS_BRIDGE_SHORT_NAME) {
        let path = PathBuf::from(path);
        let bridge_server =
            UnixSeqpacketListener::bind(path.clone()).context("Failed to bind bridge")?;
        let conn = bridge_server.accept().context("Failed to accept bridge");
        let _ = remove_file(path);
        state.borrow_mut().mms_bridge = Some(conn?);
    }

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

    match kmsg::KmsgReader::new(KMSG_PATH, state.clone()) {
        Ok(km) => ctx.add_event(Box::new(km)).unwrap(),
        Err(e) => error!("Unable to open /dev/kmsg for reading: {}", e),
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
    let mut next_expiration = None;
    while !ctx.is_empty() {
        if let Err(e) = ctx.run_once() {
            error!("{}", e);
        };
        handle_closed_child_processes(state.deref());
        if next_expiration.map(|e| e < Instant::now()).unwrap_or(true) {
            next_expiration = state.borrow_mut().cleanup_expired();
        }
    }

    Ok(())
}
