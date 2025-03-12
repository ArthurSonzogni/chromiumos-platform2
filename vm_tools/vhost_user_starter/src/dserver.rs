// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::error::Error;
use std::os::fd::AsRawFd;
use std::os::unix::process::ExitStatusExt;
use std::process::Child;
use std::process::Command;
use std::sync::mpsc::channel;
use std::sync::mpsc::Receiver;
use std::sync::mpsc::Sender;
use std::sync::Arc;
use std::sync::Mutex;
use std::vec::Vec;

use dbus::arg;
use dbus::blocking::SyncConnection;
use dbus::channel::MatchingReceiver;
use dbus_crossroads::Crossroads;
use log::error;
use log::info;
use log::warn;
use nix::fcntl;
use nix::sys::signal;
use nix::sys::signalfd;
use protobuf::Message;
use system_api::server::register_org_chromium_vhost_user_starter;
use system_api::server::OrgChromiumVhostUserStarter;
use system_api::vhost_user_starter::IdMapItem;
use system_api::vhost_user_starter::StartVhostUserFsRequest;
use system_api::vhost_user_starter::StartVhostUserFsResponse;
use system_api::vhost_user_starter::VhostUserVirtioFsConfig;

pub const DBUS_SERVICE_NAME: &str = "org.chromium.VhostUserStarter";
pub const DBUS_OBJECT_PATH: &str = "/org/chromium/VhostUserStarter";

/// Convert the protobuf message to a byte buffers. Return dbus::MethodErr when conversion fails.
fn write_dbus_response_to_bytes<M: Message>(msg: &M) -> Result<Vec<u8>, dbus::MethodErr> {
    msg.write_to_bytes().map_err(|e| {
        error!("Failed to write protobuf {} to bytes: {}", M::NAME, e);
        dbus::MethodErr::failed("Failed to write protobuf.")
    })
}

/// Parse the byte buffer to the protobuf message. Return dbus::MethodErr when failed to parse.
fn parse_dbus_request_from_bytes<M: Message>(bytes: &[u8]) -> Result<M, dbus::MethodErr> {
    M::parse_from_bytes(bytes).map_err(|e| {
        error!("Failed to parse {}: {}", M::NAME, e);
        dbus::MethodErr::failed("Failed to parse protobuf.")
    })
}

fn parse_ugid_map_to_string(ugid_map: Vec<IdMapItem>) -> Result<String, dbus::MethodErr> {
    if ugid_map.is_empty() {
        error!("ugid map is empty");
        return Err(dbus::MethodErr::failed("ugid map is empty"));
    }

    let result = ugid_map
        .iter()
        .map(|item| format!("{} {} {}", item.in_id, item.out_id, item.range))
        .collect::<Vec<String>>()
        .join(",");

    Ok(result)
}

fn parse_vhost_user_fs_cfg_to_string(fs_cfg: &VhostUserVirtioFsConfig) -> String {
    let mut v = vec![
        format!("cache={}", fs_cfg.cache),
        format!("timeout={}", fs_cfg.timeout),
        format!("rewrite-security-xattrs={}", fs_cfg.rewrite_security_xattrs),
        format!("writeback={}", fs_cfg.writeback),
        format!("negative_timeout={}", fs_cfg.negative_timeout),
        format!("ascii_casefold={}", fs_cfg.ascii_casefold),
        format!("posix_acl={}", fs_cfg.posix_acl),
    ];

    if fs_cfg.max_dynamic_perm != 0 {
        v.push(format!("max_dynamic_perm={}", fs_cfg.max_dynamic_perm))
    }
    if fs_cfg.max_dynamic_xattr != 0 {
        v.push(format!("max_dynamic_xattr={}", fs_cfg.max_dynamic_xattr))
    }

    if !fs_cfg.privileged_quota_uids.is_empty() {
        let quota_uids = "privileged_quota_uids=".to_string()
            + &fs_cfg
                .privileged_quota_uids
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<String>>()
                .join(" ");
        v.push(quota_uids);
    }
    v.join(",")
}

fn clear_cloexec(fd: i32) -> Result<(), std::io::Error> {
    let flags = fcntl::fcntl(fd, fcntl::F_GETFD)?;
    if flags == -1 {
        error!("Failed to get file descriptor flags");
        return Err(std::io::Error::last_os_error());
    }

    let new_flags = fcntl::FdFlag::from_bits_retain(flags & !libc::FD_CLOEXEC);

    let result = fcntl::fcntl(fd.as_raw_fd(), fcntl::F_SETFD(new_flags))?;
    if result != 0 {
        error!("Failed to set file descriptor flags: result={result}");
        return Err(std::io::Error::last_os_error());
    }

    Ok(())
}

/// Receives child processes from the provided channel and adds them to the `children` vector.
///
/// This allows the process spawning devices sends `Child` to  the main process. The main
/// process takes charge of management of these child processes.
fn try_receive_children(child_receiver: &Receiver<Child>, children: &mut Vec<Child>) {
    loop {
        match child_receiver.try_recv() {
            Ok(child) => {
                children.push(child);
            }
            // no more child is on the channel
            Err(std::sync::mpsc::TryRecvError::Empty) => {
                break;
            }
            Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                panic!("child channel is disconnected");
            }
        }
    }
}

/// Checks the exit status of child processes and removes completed ones.
///
/// This function will be called after reading SIGCHLD from signal fd. There's a
/// possibility that not all devices have been exited at the time. So, it uses
/// non-blocking wait to reap the child.
fn try_reap_children(children: &mut Vec<Child>) {
    children.retain_mut(|child| {
        let mut retain_child = true;
        let pid = child.id();
        match child.try_wait() {
            Ok(Some(status)) if status.success() => {
                info!("Child({}) exits successfully", pid);
                retain_child = false
            }
            Ok(Some(status)) => {
                // on failure
                if let Some(code) = status.code() {
                    // vhost_user backend device exit with error code
                    panic!("Child({}) exit with error: {}", pid, code);
                } else {
                    // The spawned child process of vhost_user
                    // backend device is killed by signal.
                    panic!("Child({}) killed by {:?}", pid, status.signal());
                }
            }
            Ok(None) => {}
            Err(e) => {
                warn!("error attempting to wait: {}", e);
            }
        }
        retain_child
    });
}

// If there's no child process and already received the SIGTERM, exit with status 0
fn try_exit(children: &[Child], received_sigterm: bool) {
    if children.is_empty() && received_sigterm {
        info!("exited successfully by SIGTERM");
        std::process::exit(0);
    }
}

// Parses a StartVhostUserFsRequest request and constructs arguments for starting vhost-user-fs.
fn prepare_vhost_user_fs_args(
    request: Vec<u8>,
    fd: &arg::OwnedFd,
) -> Result<Vec<String>, dbus::MethodErr> {
    let StartVhostUserFsRequest {
        shared_dir,
        cfg,
        tag,
        uid,
        gid,
        uid_map,
        gid_map,
        ..
    } = parse_dbus_request_from_bytes::<StartVhostUserFsRequest>(&request)?;

    let uid_map = parse_ugid_map_to_string(uid_map)?;
    let gid_map = parse_ugid_map_to_string(gid_map)?;
    let fs_cfg = parse_vhost_user_fs_cfg_to_string(&cfg);

    let mut fs_args = vec![
        "device".to_string(),
        "fs".to_string(),
        format!("--fd={}", fd.as_raw_fd()),
        format!("--tag={}", tag),
        format!("--shared-dir={}", shared_dir),
        format!("--uid-map={}", uid_map),
        format!("--gid-map={}", gid_map),
        format!("--cfg={}", fs_cfg),
    ];

    if let Some(uid) = uid {
        fs_args.push(format!("--uid={}", uid));
    }
    if let Some(gid) = gid {
        fs_args.push(format!("--gid={}", gid));
    }

    Ok(fs_args)
}

pub struct StarterService {
    /// child_sender is used to add new child processes to the monitoring loop.
    child_sender: Sender<Child>,
}

impl StarterService {
    fn new(child_sender: Sender<Child>) -> Self {
        StarterService { child_sender }
    }
}

impl OrgChromiumVhostUserStarter for StarterService {
    fn start_vhost_user_fs(
        &mut self,
        request: Vec<u8>,
        socket: Vec<arg::OwnedFd>,
    ) -> Result<Vec<u8>, dbus::MethodErr> {
        info!("vhost_user_starter received start vhost user fs request");
        let response = StartVhostUserFsResponse::new();
        if socket.is_empty() {
            return Err(dbus::MethodErr::failed("Socket fd not found"));
        }
        if socket.len() > 1 {
            return Err(dbus::MethodErr::failed("Too many socket fd"));
        }

        let fd_ref = &socket[0];
        clear_cloexec(fd_ref.as_raw_fd()).expect("Fail to clear FD_CLOEXEC");

        let fs_args = prepare_vhost_user_fs_args(request, fd_ref)?;

        let child = Command::new("crosvm")
            .args(fs_args)
            .spawn()
            .expect("Failed to execute command");

        self.child_sender
            .send(child)
            .expect("Failed to send Child to main process");

        write_dbus_response_to_bytes::<StartVhostUserFsResponse>(&response)
    }
}

/// Serve the D-Bus SyncConnection.
///
/// This function is forked from Crossroads::serve(), because Crossroads::serve() only accepts
/// dbus::blocking::Connection. The parameter `cr` is wrapped by Arc<Mutex<_>> for making it Sync.
fn serve_sync_connection(
    cr: Arc<Mutex<Crossroads>>,
    connection: dbus::blocking::SyncConnection,
    child_receiver: Receiver<Child>,
) -> Result<(), dbus::Error> {
    connection.start_receive(
        dbus::message::MatchRule::new_method_call(),
        Box::new(move |msg, conn| {
            cr.lock()
                .expect("Fail to lock crossroads")
                .handle_message(msg, conn)
                .expect("Failed to handle message");
            true
        }),
    );

    let mut mask = signalfd::SigSet::empty();
    mask.add(signal::SIGCHLD);
    mask.add(signal::SIGTERM);
    mask.thread_block().expect("Failed to block signal");
    let mut sfd = signalfd::SignalFd::with_flags(&mask, signalfd::SfdFlags::empty())
        .expect("Failed to create signal fd");

    // No need to join, since it will live as long as main thread
    let _handler = std::thread::spawn(move || loop {
        let one_day_seconds = 60 * 60 * 24;
        connection
            .process(std::time::Duration::from_secs(one_day_seconds))
            .expect("Failed to process dbus request");
    });

    let mut children = Vec::<Child>::new();
    let mut received_sigterm = false;

    // Serve clients forever.
    loop {
        match sfd.read_signal() {
            Ok(Some(signal)) => {
                if signal.ssi_signo == libc::SIGCHLD as u32 {
                    try_receive_children(&child_receiver, &mut children);
                    try_reap_children(&mut children);
                    try_exit(&children, received_sigterm);
                } else if signal.ssi_signo == libc::SIGTERM as u32 {
                    received_sigterm = true;
                    try_receive_children(&child_receiver, &mut children);
                    try_exit(&children, received_sigterm);
                } else {
                    unreachable!("Only signals in mask should be read from sfd");
                }
            }
            Ok(None) => {
                unreachable!("The signalfd is set to blocking, this line won't be reached");
            }
            Err(err) => {
                panic!("Failed to read signalfd: {}", err)
            }
        }
    }
}

pub fn service_main() -> Result<(), Box<dyn Error>> {
    // Connect to D-Bus.
    let dbus_connection = SyncConnection::new_system().map_err(|e| {
        error!("Failed to connect to D-Bus: {}", e);
        e
    })?;
    dbus_connection
        .request_name(DBUS_SERVICE_NAME, false, true, false)
        .map_err(|e| {
            error!(
                "Failed to request the service name {}: {}",
                DBUS_SERVICE_NAME, e
            );
            e
        })?;

    // Create a new crossroads instance, register the IfaceToken, and insert it to the service path.
    let mut crossroad = Crossroads::new();
    let iface_token = register_org_chromium_vhost_user_starter(&mut crossroad);

    let (sender, receiver) = channel();
    crossroad.insert(
        DBUS_OBJECT_PATH,
        &[iface_token],
        StarterService::new(sender),
    );

    // Run the D-Bus service forever.
    info!("Starting the vhost_user_starter D-Bus daemon");
    serve_sync_connection(Arc::new(Mutex::new(crossroad)), dbus_connection, receiver).map_err(
        |e| {
            error!("Failed to serve the daemon: {}", e);
            e
        },
    )?;
    unreachable!()
}

#[cfg(test)]
mod tests {

    use std::os::fd::OwnedFd;

    use super::*;
    use tempfile::tempfile;

    #[test]
    fn test_write_dbus_response_to_bytes() {
        let response = StartVhostUserFsResponse::new();
        let bytes = response.write_to_bytes().expect("write to bytes failed");
        let bytes_suc = write_dbus_response_to_bytes::<StartVhostUserFsResponse>(&response)
            .expect("write to bytes failed");
        assert_eq!(bytes, bytes_suc);
    }

    #[test]
    fn test_parse_dbus_request_from_bytes() {
        let mut request = StartVhostUserFsRequest::new();
        request.shared_dir = String::from("/tmp");
        request.tag = String::from("test");
        request.uid = Some(1234);
        request.gid = Some(5678);

        let bytes = request.write_to_bytes().expect("write to bytes failed");
        let parsed_request = parse_dbus_request_from_bytes::<StartVhostUserFsRequest>(&bytes)
            .expect("parse from bytes failed");
        assert_eq!(request, parsed_request);

        // test parse from incomplete bytes
        let mid = bytes.len() / 2;
        let incomplete_bytes = &bytes[..mid].to_vec();
        let _ = parse_dbus_request_from_bytes::<StartVhostUserFsRequest>(incomplete_bytes)
            .expect_err("parse from bytes failed");
    }

    #[test]
    fn test_parse_ugid_map_to_string() {
        let ugid_map: Vec<IdMapItem> = vec![
            IdMapItem {
                in_id: 0,
                out_id: 0,
                range: 1,
                special_fields: protobuf::SpecialFields::new(),
            },
            IdMapItem {
                in_id: 1000,
                out_id: 1000,
                range: 10,
                special_fields: protobuf::SpecialFields::new(),
            },
            IdMapItem {
                in_id: 1001,
                out_id: 655360,
                range: 1000000,
                special_fields: protobuf::SpecialFields::new(),
            },
        ];

        let str = parse_ugid_map_to_string(ugid_map).expect("parse ugid map failed");
        assert_eq!(str, "0 0 1,1000 1000 10,1001 655360 1000000");
    }

    #[test]
    fn test_parse_vhost_user_fs_cfg_to_string() {
        let mut cfg = VhostUserVirtioFsConfig::new();
        cfg.cache = String::from("auto");
        cfg.timeout = 1;
        cfg.rewrite_security_xattrs = true;
        cfg.writeback = false;
        cfg.negative_timeout = 1;
        cfg.ascii_casefold = false;
        cfg.posix_acl = true;

        let str = parse_vhost_user_fs_cfg_to_string(&cfg);
        assert_eq!(
            str,
            "cache=auto,timeout=1,rewrite-security-xattrs=true,writeback=false,negative_timeout=1,\
            ascii_casefold=false,posix_acl=true",
        );
    }

    #[test]
    fn test_parse_vhost_user_fs_cfg_to_string_with_dynamic_permission_and_xattr() {
        let mut cfg = VhostUserVirtioFsConfig::new();
        cfg.cache = String::from("auto");
        cfg.timeout = 1;
        cfg.rewrite_security_xattrs = true;
        cfg.writeback = false;
        cfg.negative_timeout = 1;
        cfg.ascii_casefold = false;
        cfg.posix_acl = true;
        cfg.max_dynamic_perm = 2;
        cfg.max_dynamic_xattr = 2;

        let str = parse_vhost_user_fs_cfg_to_string(&cfg);
        assert_eq!(
            str,
            "cache=auto,timeout=1,rewrite-security-xattrs=true,writeback=false,negative_timeout=1,\
            ascii_casefold=false,posix_acl=true,max_dynamic_perm=2,max_dynamic_xattr=2",
        );
    }

    #[test]
    fn test_clear_cloexec() {
        // Create a temporary file
        let file = tempfile().unwrap();
        let fd = file.as_raw_fd();

        // Set the close-on-exec flag
        let flags = fcntl::fcntl(fd, fcntl::F_GETFD).unwrap();
        let flags = fcntl::FdFlag::from_bits_retain(flags | libc::FD_CLOEXEC);
        fcntl::fcntl(fd, fcntl::F_SETFD(flags)).unwrap();

        // Clear the close-on-exec flag using our function
        clear_cloexec(fd).unwrap();

        // Check that the close-on-exec flag is cleared
        let flags = fcntl::fcntl(fd, fcntl::F_GETFD).unwrap();
        assert_eq!(flags & libc::FD_CLOEXEC, 0);
    }

    #[test]
    fn test_prepare_vhost_user_fs_args() {
        // The request simulate the request from concierge except for socket fd

        // Create a temporary file
        let file = tempfile().unwrap();
        let fd: OwnedFd = file.into();
        let fd_raw = fd.as_raw_fd();

        let mut request = StartVhostUserFsRequest::new();
        request.tag = "stub".to_owned();
        request.shared_dir = "/run/arcvm/media".to_owned();
        request.uid = Some(0);
        request.gid = Some(0);

        // Prepare --cfg={} field
        request.cfg = protobuf::MessageField::some(VhostUserVirtioFsConfig {
            cache: String::from("auto"),
            timeout: 1,
            rewrite_security_xattrs: true,
            ascii_casefold: false,
            writeback: false,
            posix_acl: true,
            negative_timeout: 1,
            privileged_quota_uids: vec![0],
            max_dynamic_perm: 2,
            max_dynamic_xattr: 2,
            ..Default::default()
        });

        // Prepare ugid_map field
        request.uid_map = vec![IdMapItem {
            in_id: 0,
            out_id: 1000,
            range: 1,
            special_fields: protobuf::SpecialFields::new(),
        }];
        request.gid_map = vec![
            IdMapItem {
                in_id: 0,
                out_id: 1001,
                range: 1,
                special_fields: protobuf::SpecialFields::new(),
            },
            IdMapItem {
                in_id: 1000,
                out_id: 656360,
                range: 1,
                special_fields: protobuf::SpecialFields::new(),
            },
        ];

        // Convert DBus request to raw data
        let mut request_vec = Vec::<u8>::new();
        request
            .write_to_vec(&mut request_vec)
            .expect("failed to write request to vec");

        let fs_args =
            prepare_vhost_user_fs_args(request_vec, &fd).expect("failed to prepare fs args");

        assert_eq!(
            fs_args,
            vec![
                "device",
                "fs",
                format!("--fd={}", fd_raw).as_str(),
                "--tag=stub",
                "--shared-dir=/run/arcvm/media",
                "--uid-map=0 1000 1",
                "--gid-map=0 1001 1,1000 656360 1",
                "--cfg=cache=auto,timeout=1,rewrite-security-xattrs=true,writeback=false,\
                negative_timeout=1,ascii_casefold=false,posix_acl=true,max_dynamic_perm=2,\
                max_dynamic_xattr=2,privileged_quota_uids=0",
                "--uid=0",
                "--gid=0",
            ]
        );
    }
}
