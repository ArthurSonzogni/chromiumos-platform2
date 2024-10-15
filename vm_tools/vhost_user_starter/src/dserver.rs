// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::error::Error;
use std::os::fd::AsFd;
use std::os::fd::AsRawFd;
use std::process::Command;
use std::process::ExitStatus;
use std::sync::Arc;
use std::sync::Mutex;
use std::thread::JoinHandle;
use std::vec::Vec;

use dbus::arg;
use dbus::blocking::SyncConnection;
use dbus::channel::MatchingReceiver;
use dbus_crossroads::Crossroads;
use log::error;
use log::info;
use nix::fcntl;
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

    if !fs_cfg.privileged_quota_uids.is_empty() {
        let mut quota_uids = "privileged_quota_uids=".to_string();
        quota_uids += &fs_cfg
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

#[derive(Default)]
pub struct StarterService {
    /// keep_fds helps vhost-user device enter Linux jails without closing
    /// needed file descriptors. Vhost_user_starter will generate Uuid for
    /// each device to serve as the key. The value is socket fd used to communicate
    /// with vhost_user_frontend device. Item should be dropped when the backend
    /// device exits.
    keep_fds: Arc<Mutex<HashMap<u32, arg::OwnedFd>>>,
    child_join_handles: Arc<Mutex<Vec<JoinHandle<ExitStatus>>>>,
    device_counter: u32,
}

impl StarterService {
    fn new(
        keep_fds: Arc<Mutex<HashMap<u32, arg::OwnedFd>>>,
        child_join_handles: Arc<Mutex<Vec<JoinHandle<ExitStatus>>>>,
    ) -> Self {
        StarterService {
            keep_fds,
            child_join_handles,
            device_counter: 0,
        }
    }

    fn next_device_id(&mut self) -> u32 {
        self.device_counter = self
            .device_counter
            .checked_add(1)
            .expect("Failed to increment device counter");
        self.device_counter
    }
}

impl OrgChromiumVhostUserStarter for StarterService {
    fn start_vhost_user_fs(
        &mut self,
        request: Vec<u8>,
        mut socket: Vec<arg::OwnedFd>,
    ) -> Result<Vec<u8>, dbus::MethodErr> {
        info!("vhost_user_starter received start vhost user fs request");
        let response = StartVhostUserFsResponse::new();
        if socket.is_empty() {
            return Err(dbus::MethodErr::failed("Socket fd not found"));
        }
        if socket.len() > 1 {
            return Err(dbus::MethodErr::failed("Too many socket fd"));
        }

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
        let fd = socket[0].as_fd().as_raw_fd();

        let uid_map = parse_ugid_map_to_string(uid_map)?;
        let gid_map = parse_ugid_map_to_string(gid_map)?;
        let fs_cfg = parse_vhost_user_fs_cfg_to_string(&cfg);

        let mut fs_args = vec![
            "device".to_string(),
            "fs".to_string(),
            format!("--fd={}", fd),
            format!("--tag={}", tag),
            format!("--shared-dir={}", shared_dir),
            format!("--uid-map={}", uid_map),
            format!("--gid-map={}", gid_map),
            format!("--cfg={}", fs_cfg),
        ];

        // uid/gid field is optional, if no set the default value is 0
        if let Some(uid) = uid {
            fs_args.push(format!("--uid={}", uid));
        }
        if let Some(gid) = gid {
            fs_args.push(format!("--gid={}", gid));
        }

        clear_cloexec(fd).expect("Fail to clear FD_CLOEXEC");

        let device_id = self.next_device_id();
        let owned_socket_fd = socket.pop().expect("Checked socket len is 1");

        let keep_fds = Arc::clone(&self.keep_fds);
        keep_fds
            .lock()
            .expect("Fail to lock keep_fds")
            .insert(device_id, owned_socket_fd);

        let mut cmd = Command::new("crosvm")
            .args(fs_args)
            .spawn()
            .expect("Failed to execute command");

        let handler = std::thread::spawn(move || {
            let exit_status = cmd.wait().expect("Failed to wait on child");
            info!("Child process exited with: {}", exit_status);

            // After vhost_user_device exits, the backend socket fd should be closed.
            keep_fds
                .lock()
                .expect("Fail to lock keep_fds")
                .remove(&device_id)
                .expect("Owned_socket_fd not found");
            exit_status
        });

        self.child_join_handles
            .lock()
            .expect("Fail to lock child_join_handles")
            .push(handler);

        write_dbus_response_to_bytes::<StartVhostUserFsResponse>(&response)
    }
}

/// Serve the D-Bus SyncConnection.
///
/// This function is forked from Crossroads::serve(), because Crossroads::serve() only accepts
/// dbus::blocking::Connection. The parameter `cr` is wrapped by Arc<Mutex<_>> for making it Sync.
fn serve_sync_connection(
    cr: Arc<Mutex<Crossroads>>,
    connection: &dbus::blocking::SyncConnection,
    child_join_handles: Arc<Mutex<Vec<JoinHandle<ExitStatus>>>>,
) -> Result<(), dbus::Error> {
    connection.start_receive(
        dbus::message::MatchRule::new_method_call(),
        Box::new(move |msg, conn| {
            cr.lock()
                .expect("Fail to lock crossroads")
                .handle_message(msg, conn)
                .unwrap();
            true
        }),
    );
    // Serve clients forever.
    loop {
        connection.process(std::time::Duration::from_millis(1000))?;
        let mut handlers = child_join_handles
            .lock()
            .expect("Fail to get child_join_handler's lock");
        for i in 0..handlers.len() {
            if handlers[i].is_finished() {
                let handler = handlers.remove(i);
                match handler.join() {
                    // OK
                    Ok(status) => {
                        if !status.success() {
                            if let Some(code) = status.code() {
                                // vhost_user backend device exit with error code
                                panic!("vhost_user backend device exit with error code: {code}");
                            } else {
                                // The spawned child process of vhost_user backend device is
                                // killed by signal
                                panic!("vhost_user backend device killed by signal");
                            }
                        }
                    }
                    // When crash reporter failed to detect reaper thread panic
                    Err(e) => {
                        panic!("Child process failed to exit normally: {:?}", e);
                    }
                }
            }
        }
    }
}

pub fn service_main() -> Result<(), Box<dyn Error>> {
    // Connect to D-Bus.
    let dbus_connection = Arc::new(SyncConnection::new_system().map_err(|e| {
        error!("Failed to connect to D-Bus: {}", e);
        e
    })?);
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

    let child_join_handlers = Arc::new(Mutex::new(vec![]));
    let keep_fds = Arc::new(Mutex::new(HashMap::new()));

    crossroad.insert(
        DBUS_OBJECT_PATH,
        &[iface_token],
        StarterService::new(keep_fds.clone(), child_join_handlers.clone()),
    );

    // Run the D-Bus service forever.
    info!("Starting the vhost_user_starter D-Bus daemon");
    serve_sync_connection(
        Arc::new(Mutex::new(crossroad)),
        &dbus_connection,
        child_join_handlers.clone(),
    )
    .map_err(|e| {
        error!("Failed to serve the daemon: {}", e);
        e
    })?;
    unreachable!()
}

#[cfg(test)]
mod tests {

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

        let expect_str = String::from(
            "cache=auto,timeout=1,rewrite-security-xattrs=true,writeback=false,negative_timeout=1,\
            ascii_casefold=false,posix_acl=true",
        );
        let str = parse_vhost_user_fs_cfg_to_string(&cfg);
        assert_eq!(str, expect_str);
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
}
