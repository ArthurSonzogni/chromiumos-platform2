// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::convert::{TryFrom, TryInto};
use std::error::Error;
use std::fmt;
use std::fs::File;
use std::iter::FromIterator;
use std::os::fd::OwnedFd;
use std::os::unix::io::AsRawFd;
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::channel;
use std::thread::sleep;
use std::time::Duration;

use dbus::{
    blocking::{self, BlockingSender, Connection},
    channel::MatchingReceiver,
    message::MatchRule,
    strings::{Interface, Member},
    Message,
};
use libchromeos::chromeos::is_dev_mode;
use libchromeos::OpenSafelyOptions;
use protobuf::EnumOrUnknown;
use protobuf::Message as ProtoMessage;
use system_api::cicerone_service::*;
use system_api::client::OrgChromiumDebugd;
use system_api::client::OrgChromiumDlcServiceInterface;
use system_api::client::OrgChromiumPermissionBroker;
use system_api::client::OrgChromiumSessionManagerInterface;
use system_api::client::OrgChromiumVmCicerone;
use system_api::client::OrgChromiumVmConcierge;
use system_api::concierge_service::vm_info::VmType;
use system_api::concierge_service::*;
use system_api::dlcservice;
use system_api::dlcservice::dlc_state;
use system_api::dlcservice::DlcState;
use system_api::launch::*;
use system_api::seneschal_service::*;
use system_api::vm_plugin_dispatcher;
use system_api::vm_plugin_dispatcher::VmErrorCode;

use crate::disk::{DiskInfo, DiskOpType, VmDiskImageType, VmState};
use crate::proto::system_api::*;

const REMOVABLE_MEDIA_ROOT: &str = "/media/removable";
const CRYPTOHOME_USER: &str = "/home/user";
const DOWNLOADS_DIR: &str = "Downloads";
const MY_FILES_DIR: &str = "MyFiles";
const MNT_SHARED_ROOT: &str = "/mnt/shared";

/// Round to disk block size.
const DEFAULT_TIMEOUT: Duration = Duration::from_millis(80 * 1000);
const EXPORT_DISK_TIMEOUT: Duration = Duration::from_millis(15 * 60 * 1000);

const DLCSERVICE_NO_IMAGE_FOUND_ERROR: &str = "org.chromium.DlcServiceInterface.NO_IMAGE_FOUND";

pub enum ChromeOSError {
    BadChromeFeatureStatus,
    BadDiskImageStatus(EnumOrUnknown<DiskImageStatus>, String),
    BadVmStatus(EnumOrUnknown<VmStatus>, String),
    BadVmPluginDispatcherStatus,
    BiosAlreadySpecified(String),
    BiosDlcNotAllowed(String),
    CrostiniVmDisabled,
    CrostiniVmDisabledReason(String),
    DiskImageOutOfSpace,
    FailedAdjustVm(String),
    FailedAttachKey(String),
    FailedAttachUsb(String),
    FailedAllocateExtraDisk {
        path: PathBuf,
        reason: String,
    },
    FailedCreateContainer(EnumOrUnknown<create_lxd_container_response::Status>, String),
    FailedCreateContainerSignal(EnumOrUnknown<lxd_container_created_signal::Status>, String),
    FailedDetachUsb(String),
    FailedDlcInstall(String, String),
    FailedGetOpenPath(PathBuf),
    FailedGetVmInfo,
    FailedListDiskImages(String),
    FailedListUsb,
    FailedMetricsSend {
        exit_code: Option<i32>,
    },
    FailedAttachUsbToContainer(
        EnumOrUnknown<attach_usb_to_container_response::Status>,
        String,
    ),
    FailedSendProblemReport(String, i32),
    FailedSetUpVmUser(String),
    FailedSetupContainerUser(
        EnumOrUnknown<set_up_lxd_container_user_response::Status>,
        String,
    ),
    FailedSharePath(String),
    FailedStartContainerStatus(EnumOrUnknown<start_lxd_container_response::Status>, String),
    FailedLxdContainerStarting(EnumOrUnknown<lxd_container_starting_signal::Status>, String),
    FailedStartLxdProgressSignal(EnumOrUnknown<start_lxd_progress_signal::Status>, String),
    FailedStartLxdStatus(EnumOrUnknown<start_lxd_response::Status>, String),
    FailedStopVm {
        vm_name: String,
        reason: String,
    },
    FailedUpdateContainerDevices(String),
    FakeConnectionProxy,
    GetIoDevices,
    InvalidDiskSize(u64),
    InvalidEmail,
    InvalidPath(String),
    MissingActiveSession,
    NoSuchVm,
    NoSuchVmType,
    NoVmTechnologyEnabled,
    NotAvailableForPluginVm,
    NotPluginVm,
    OpenUserFile(PathBuf, std::io::Error),
    PluginVmDisabled,
    PluginVmDisabledReason(String),
    PluginVmGenericError(i32),
    PluginVmLicenseExpired(i32),
    PluginVmLicenseInvalid(i32),
    PluginVmNotEnoughDisk,
    PluginVmNoPortalAccess,
    RetrieveActiveSessions,
    ToolsDlcNotAllowed(String),
}

use self::ChromeOSError::*;

impl fmt::Display for ChromeOSError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BadChromeFeatureStatus => write!(f, "invalid response to chrome feature request"),
            BadDiskImageStatus(s, reason) => {
                write!(f, "bad disk image status: `{:?}`: {}", s, reason)
            }
            BadVmStatus(s, reason) => write!(f, "bad VM status: `{:?}`: {}", s, reason),
            BadVmPluginDispatcherStatus => write!(f, "failed to start Parallels dispatcher"),
            BiosAlreadySpecified(dlc) => write!(
                f,
                "bios path already specified bios dlc `{}` is not allowed",
                dlc
            ),
            BiosDlcNotAllowed(dlc) => write!(f, "bios dlc `{}` is not allowed", dlc),
            CrostiniVmDisabled => write!(f, "Crostini VMs are not available"),
            CrostiniVmDisabledReason(reason) => {
                write!(f, "Crostini VMs are not available: {}", reason)
            }
            DiskImageOutOfSpace => write!(f, "not enough disk space"),
            FailedAdjustVm(reason) => write!(f, "failed to adjust vm: {}", reason),
            FailedAttachKey(reason) => write!(f, "failed to attach security key to vm: {}", reason),
            FailedAttachUsb(reason) => write!(f, "failed to attach usb device to vm: {}", reason),
            FailedAllocateExtraDisk { path, reason } => write!(
                f,
                "failed to allocate an extra disk at {}: {}",
                path.display(),
                reason
            ),
            FailedDetachUsb(reason) => write!(f, "failed to detach usb device from vm: {}", reason),
            FailedDlcInstall(name, reason) => write!(
                f,
                "DLC service failed to install module `{}`: {}",
                name, reason
            ),
            FailedCreateContainer(s, reason) => {
                write!(f, "failed to create container: `{:?}`: {}", s, reason)
            }
            FailedCreateContainerSignal(s, reason) => {
                write!(f, "failed to create container: `{:?}`: {}", s, reason)
            }
            FailedGetOpenPath(path) => write!(f, "failed to request OpenPath {}", path.display()),
            FailedGetVmInfo => write!(f, "failed to get vm info"),
            FailedAttachUsbToContainer(s, reason) => {
                write!(
                    f,
                    "failed to register shared USB device with container: `{:?}`: {}",
                    s, reason
                )
            }
            FailedSendProblemReport(msg, error_code) => {
                write!(f, "failed to send problem report: {} ({})", msg, error_code)
            }
            FailedSetUpVmUser(reason) => {
                write!(f, "failed to set up VM user: {}", reason)
            }
            FailedSetupContainerUser(s, reason) => {
                write!(f, "failed to setup container user: `{:?}`: {}", s, reason)
            }
            FailedSharePath(reason) => write!(f, "failed to share path with vm: {}", reason),
            FailedStartContainerStatus(s, reason) => {
                write!(f, "failed to start container: `{:?}`: {}", s, reason)
            }
            FailedLxdContainerStarting(s, reason) => {
                write!(f, "failed to start container: `{:?}`: {}", s, reason)
            }
            FailedStartLxdProgressSignal(s, reason) => {
                write!(f, "failed to start lxd: `{:?}`: {}", s, reason)
            }
            FailedStartLxdStatus(s, reason) => {
                write!(f, "failed to start lxd: `{:?}`: {}", s, reason)
            }
            FailedListDiskImages(reason) => write!(f, "failed to list disk images: {}", reason),
            FailedListUsb => write!(f, "failed to get list of usb devices attached to vm"),
            FailedMetricsSend { exit_code } => {
                write!(f, "failed to send metrics")?;
                if let Some(code) = exit_code {
                    write!(f, ": exited with non-zero code {}", code)?;
                }
                Ok(())
            }
            FailedStopVm { vm_name, reason } => {
                write!(f, "failed to stop vm `{}`: {}", vm_name, reason)
            }
            FailedUpdateContainerDevices(reason) => {
                write!(f, "Failed to update container devices: {}", reason)
            }
            FakeConnectionProxy => {
                write!(f, "fake connection proxy for test")
            }
            GetIoDevices => write!(f, "failed to get io devices"),
            InvalidDiskSize(n) => write!(f, "invalid disk size {}", n),
            InvalidEmail => write!(f, "the active session has an invalid email address"),
            InvalidPath(p) => write!(f, "invalid path: `{}`", p),
            MissingActiveSession => write!(
                f,
                "missing active session corresponding to $CROS_USER_ID_HASH"
            ),
            NoSuchVm => write!(f, "VM with such name does not exist"),
            NoSuchVmType => write!(f, "Invalid VM type"),
            NoVmTechnologyEnabled => write!(f, "neither Crostini nor Parallels VMs are enabled"),
            NotAvailableForPluginVm => write!(f, "this command is not available for Parallels VM"),
            NotPluginVm => write!(f, "this VM is not a Parallels VM"),
            OpenUserFile(p, e) => write!(f, "failed to open `{}`: {}", p.display(), e),
            PluginVmDisabled => {
                write!(
                    f,
                    "Parallels VMs are not available. Visit `chrome://vm/parallels` \
                           page in Chrome browser to review state of Parallels Desktop software."
                )
            }
            PluginVmDisabledReason(reason) => {
                write!(
                    f,
                    "Parallels VMs are not available: {}. Visit `chrome://vm/parallels` \
                           page in Chrome browser to review state of Parallels Desktop software.",
                    reason
                )
            }
            PluginVmGenericError(rc) => write!(f, "failed to execute request: {:#X}", rc),
            PluginVmLicenseExpired(rc) => write!(f, "expired license: {:#X}", rc),
            PluginVmLicenseInvalid(rc) => write!(f, "invalid license: {:#X}", rc),
            PluginVmNotEnoughDisk => write!(f, "insufficient disk space to start VM"),
            PluginVmNoPortalAccess => write!(f, "unable to access Parallels licensing portal"),
            RetrieveActiveSessions => write!(f, "failed to retrieve active sessions"),
            ToolsDlcNotAllowed(dlc) => write!(f, "tools dlc `{}` is not allowed", dlc),
        }
    }
}

impl fmt::Debug for ChromeOSError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        <Self as fmt::Display>::fmt(self, f)
    }
}

impl Error for ChromeOSError {}

fn dbus_message_to_proto<T: ProtoMessage>(message: &Message) -> Result<T, Box<dyn Error>> {
    let raw_buffer: Vec<u8> = message.read1()?;
    let mut proto = T::new();
    proto.merge_from_bytes(&raw_buffer)?;
    Ok(proto)
}

#[derive(Default)]
pub struct VmFeatures {
    pub gpu: bool,
    pub dgpu_passthrough: bool,
    pub big_gl: bool,
    pub virtgpu_native_context: bool,
    pub vtpm_proxy: bool,
    pub audio_capture: bool,
    pub dlc: Option<String>,
    pub kernel_params: Vec<String>,
    pub tools_dlc_id: Option<String>,
    pub timeout: u32,
    pub oem_strings: Vec<String>,
    pub bios_dlc_id: Option<String>,
    pub vm_type: Option<VmType>,
}

pub enum ContainerSource {
    ImageServer {
        image_alias: String,
        image_server: String,
    },
    Tarballs {
        rootfs_path: String,
        metadata_path: String,
    },
}

impl Default for ContainerSource {
    fn default() -> Self {
        ContainerSource::ImageServer {
            image_alias: "".to_string(),
            image_server: "".to_string(),
        }
    }
}

struct ProtobusSignalWatcher<'a> {
    connection: Option<&'a Connection>,
    interface: Interface<'a>,
    signal: Member<'a>,
}

impl<'a> ProtobusSignalWatcher<'a> {
    fn new(
        connection: Option<&'a Connection>,
        interface: &str,
        signal: &str,
    ) -> Result<ProtobusSignalWatcher<'a>, Box<dyn Error>> {
        let out = ProtobusSignalWatcher {
            connection,
            interface: interface.to_owned().into(),
            signal: signal.to_owned().into(),
        };
        if let Some(connection) = out.connection {
            connection.add_match_no_cb(&out.match_rule().match_str())?;
        }
        Ok(out)
    }

    fn match_rule(&self) -> MatchRule {
        MatchRule::new_signal(&self.interface, &self.signal)
    }

    fn wait<O: ProtoMessage>(&self, timeout: Duration) -> Result<O, Box<dyn Error>> {
        self.wait_with_filter(timeout, |_| true)
    }

    fn wait_with_filter<O, F>(&self, timeout: Duration, predicate: F) -> Result<O, Box<dyn Error>>
    where
        O: ProtoMessage,
        F: Fn(&O) -> bool,
    {
        let connection = match self.connection {
            Some(c) => c,
            None => return Err("waiting for items on mocked connections is not implemented".into()),
        };

        // Need a channel to get a result out of the 'static life-time callback of start_receive
        let (sender, receiver) = channel();

        let callback_token = connection.start_receive(
            self.match_rule().static_clone(),
            Box::new(move |message: Message, _: &Connection| {
                sender
                    .send(
                        dbus_message_to_proto(&message)
                            .map_err(|e| format!("Failed to parse a protobuf: {e:#?}")),
                    )
                    .expect("Failed to pass a proto via a channel");
                true
            }),
        );

        let result = loop {
            if !connection.process(timeout)? {
                break Err("timeout while waiting for a signal".into());
            }
            let proto_message = match receiver.try_recv() {
                Ok(proto_message) => proto_message?,
                // There's no message when we received a non-matching dbus message
                Err(std::sync::mpsc::TryRecvError::Empty) => continue,
                Err(e) => break Err(format!("Failed to receive a message: {e:#?}").into()),
            };
            if predicate(&proto_message) {
                break Ok(proto_message);
            }
        };
        connection
            .remove_match(callback_token)
            .map_err(|_| "Failed to remove a signal callback")?;

        result
    }
}

impl Drop for ProtobusSignalWatcher<'_> {
    fn drop(&mut self) {
        if let Some(connection) = self.connection {
            let _ = connection.remove_match_no_cb(&self.match_rule().match_str());
        }
    }
}

pub trait FilterFn: 'static + Fn(Message) -> Result<Message, Result<Message, dbus::Error>> {}

impl<T> FilterFn for T where
    T: 'static + Fn(Message) -> Result<Message, Result<Message, dbus::Error>>
{
}

#[derive(Default)]
pub struct ConnectionProxy {
    connection: Option<Connection>,
    filter: Option<Box<dyn FilterFn>>,
}

impl From<Connection> for ConnectionProxy {
    fn from(connection: Connection) -> ConnectionProxy {
        ConnectionProxy {
            connection: Some(connection),
            ..Default::default()
        }
    }
}

impl ConnectionProxy {
    #[cfg(test)]
    pub fn dummy() -> ConnectionProxy {
        Default::default()
    }

    pub fn set_filter<F: FilterFn>(&mut self, filter: F) {
        self.filter = Some(Box::new(filter));
    }

    fn send_with_reply_and_block(
        &self,
        msg: Message,
        timeout: Duration,
    ) -> Result<Message, dbus::Error> {
        let mut filtered_msg = match &self.filter {
            Some(filter) => match filter(msg) {
                Ok(new_msg) => new_msg,
                Err(res) => return res,
            },
            None => msg,
        };
        match &self.connection {
            Some(connection) => connection.send_with_reply_and_block(filtered_msg, timeout),
            None => {
                // A serial number is required to assign to the method return message.
                filtered_msg.set_serial(1);
                Ok(Message::new_method_return(&filtered_msg).unwrap())
            }
        }
    }

    fn get_real_connection_or_fail(&self) -> Result<&Connection, Box<dyn Error>> {
        if let Some(conn) = &self.connection {
            Ok(conn)
        } else {
            Err(FakeConnectionProxy.into())
        }
    }
}

struct OutputFile {
    path: PathBuf,
    fd: OwnedFd,
    committed: bool,
}

impl OutputFile {
    pub fn new(file: File, path: PathBuf) -> Result<Self, Box<dyn Error>> {
        Ok(OutputFile {
            path,
            fd: file.into(),
            committed: false,
        })
    }

    pub fn commit(&mut self) {
        self.committed = true;
    }

    pub fn as_owned_fd(&self) -> &OwnedFd {
        &self.fd
    }

    pub fn remove(&mut self) {
        if !self.committed {
            let _ = std::fs::remove_file(&self.path);
            self.committed = true; // don't try to remove the file again in drop
        }
    }
}

impl Drop for OutputFile {
    fn drop(&mut self) {
        self.remove();
    }
}

struct InputFile {
    fd: OwnedFd,
    size: u64,
}

impl InputFile {
    pub fn new(file: File) -> Result<Self, Box<dyn Error>> {
        let size = file.metadata()?.len();
        Ok(InputFile {
            fd: OwnedFd::from(file),
            size,
        })
    }
}

/// Open a file from a user-specified path.
///
/// This function will resolve the path (optionally relative to a removable media device), verify
/// whether it should be allowed as a user-specified input or output filename, and open the file
/// with the specified options.
///
/// Returns the opened file and its path.
fn open_user_path(
    user_id_hash: &str,
    name: &str,
    removable_media: Option<&str>,
    open_options: &OpenSafelyOptions,
) -> Result<(File, PathBuf), Box<dyn Error>> {
    let path = match removable_media {
        Some(media_path) => Path::new(REMOVABLE_MEDIA_ROOT).join(media_path).join(name),
        None => {
            let path = PathBuf::from(name);
            if path.is_absolute() {
                // The filename was an absolute path, so use it directly.
                // It will be validated against the allowed set of root directories below.
                path
            } else {
                // The filename is relative to the Downloads directory.
                Path::new(CRYPTOHOME_USER)
                    .join(user_id_hash)
                    .join(MY_FILES_DIR)
                    .join(DOWNLOADS_DIR)
                    .join(name)
            }
        }
    };

    // Disallow any ".." path components.
    if path.components().any(|c| c == Component::ParentDir) {
        return Err(InvalidPath(name.to_string()).into());
    }

    // The path must be a descendant of one of these directories.
    let mut allowed_roots = vec![
        PathBuf::from(REMOVABLE_MEDIA_ROOT),
        Path::new(CRYPTOHOME_USER)
            .join(user_id_hash)
            .join(MY_FILES_DIR),
        PathBuf::from("/home/chronos/user/MyFiles"),
    ];

    if is_dev_mode().unwrap_or_default() {
        // Used for preloaded tast test data files.
        allowed_roots.push(PathBuf::from("/usr/local/share/tast"));
    }

    if !allowed_roots
        .iter()
        .any(|allowed_root| path.starts_with(allowed_root))
    {
        return Err(InvalidPath(name.to_string()).into());
    }

    let file = open_options
        .open(&path)
        .map_err(|e| OpenUserFile(path.clone(), e))?;

    Ok((file, path))
}

#[derive(Default)]
pub struct UserInfo {
    pub username: String,
    pub uid: Option<u32>,
    pub group_names: Vec<String>,
}

#[derive(Default)]
pub struct UserDisks {
    pub kernel: Option<String>,
    pub rootfs: Option<String>,
    pub writable_rootfs: bool,
    pub initrd: Option<String>,
    pub extra_disk: Option<String>,
    pub bios: Option<String>,
    pub pflash: Option<String>,
}

#[derive(Clone)]
pub enum VmTypeStatus {
    Enabled,
    Disabled(Option<String>),
}

fn concierge<'a>(connection: &Connection, timeout: Duration) -> blocking::Proxy<'a, &Connection> {
    connection.with_proxy(
        VM_CONCIERGE_SERVICE_NAME,
        VM_CONCIERGE_SERVICE_PATH,
        timeout,
    )
}

fn cicerone<'a>(connection: &Connection, timeout: Duration) -> blocking::Proxy<'a, &Connection> {
    connection.with_proxy(VM_CICERONE_SERVICE_NAME, VM_CICERONE_SERVICE_PATH, timeout)
}

/// Uses the standard ChromeOS interfaces to implement the methods with the least possible
/// privilege. Uses a combination of D-Bus, protobufs, and shell protocols.
pub struct Methods {
    connection: ConnectionProxy,
    crostini_enabled: Option<VmTypeStatus>,
    plugin_vm_enabled: Option<VmTypeStatus>,
}

impl Methods {
    /// Initiates a D-Bus connection and returns an initialized `Methods`.
    pub fn new() -> Result<Methods, Box<dyn Error>> {
        let connection = Connection::new_system()?;
        Ok(Methods {
            connection: connection.into(),
            crostini_enabled: None,
            plugin_vm_enabled: None,
        })
    }

    fn concierge_client(
        &self,
    ) -> Result<blocking::Proxy<&dbus::blocking::Connection>, Box<dyn Error>> {
        self.concierge_client_with_timeout(DEFAULT_TIMEOUT)
    }

    fn concierge_client_with_timeout(
        &self,
        timeout: Duration,
    ) -> Result<blocking::Proxy<&dbus::blocking::Connection>, Box<dyn Error>> {
        let proxy: blocking::Proxy<'_, _> =
            concierge(self.connection.get_real_connection_or_fail()?, timeout);
        Ok(proxy)
    }

    #[cfg(test)]
    pub fn dummy() -> Methods {
        Methods {
            connection: ConnectionProxy::dummy(),
            crostini_enabled: Some(VmTypeStatus::Enabled),
            plugin_vm_enabled: Some(VmTypeStatus::Enabled),
        }
    }

    pub fn connection_proxy_mut(&mut self) -> &mut ConnectionProxy {
        &mut self.connection
    }

    /// Helper for doing protobuf over dbus requests and responses.
    fn sync_protobus<I: ProtoMessage, O: ProtoMessage>(
        &self,
        message: Message,
        request: &I,
    ) -> Result<O, Box<dyn Error>> {
        self.sync_protobus_timeout(message, request, &[], DEFAULT_TIMEOUT)
    }

    /// Helper for doing protobuf over dbus requests and responses.
    fn sync_protobus_timeout<I: ProtoMessage, O: ProtoMessage>(
        &self,
        message: Message,
        request: &I,
        fds: &[OwnedFd],
        timeout: Duration,
    ) -> Result<O, Box<dyn Error>> {
        let method = message.append1(request.write_to_bytes()?).append_ref(fds);
        let message = self.connection.send_with_reply_and_block(method, timeout)?;
        dbus_message_to_proto(&message)
    }

    fn protobus_wait_for_signal_timeout<O: ProtoMessage>(
        &mut self,
        interface: &str,
        signal: &str,
        timeout: Duration,
    ) -> Result<O, Box<dyn Error>> {
        ProtobusSignalWatcher::new(self.connection.connection.as_ref(), interface, signal)?
            .wait(timeout)
    }

    fn get_dlc_state(&mut self, name: &str) -> Result<DlcState, Box<dyn Error>> {
        let proxy: blocking::Proxy<'_, _> = Connection::with_proxy(
            self.connection.get_real_connection_or_fail()?,
            DLC_SERVICE_SERVICE_NAME,
            DLC_SERVICE_SERVICE_PATH,
            DEFAULT_TIMEOUT,
        );
        let response: DlcState = ProtoMessage::parse_from_bytes(
            &OrgChromiumDlcServiceInterface::get_dlc_state(&proxy, name)
                .map_err(|e| FailedDlcInstall(name.to_owned(), e.to_string()))?,
        )?;
        Ok(response)
    }

    fn init_dlc_install(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        let mut install_request = dlcservice::InstallRequest::new();
        install_request.id = name.to_owned();

        let proxy: blocking::Proxy<'_, _> = Connection::with_proxy(
            self.connection.get_real_connection_or_fail()?,
            DLC_SERVICE_SERVICE_NAME,
            DLC_SERVICE_SERVICE_PATH,
            DEFAULT_TIMEOUT,
        );
        OrgChromiumDlcServiceInterface::install(&proxy, install_request.write_to_bytes()?)
            .map_err(|e| FailedDlcInstall(name.to_owned(), e.to_string()))?;
        Ok(())
    }

    fn poll_dlc_install(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        // Unfortunately DLC service does not provide a synchronous method to install package,
        // and, if package is already installed, OnInstallStatus signal might be issued before
        // replying to "Install" method call, which does not carry any indication whether the
        // operation in progress or not. So polling it is...
        while self.get_dlc_state(name)?.state.enum_value() == Ok(dlc_state::State::INSTALLING) {
            sleep(Duration::from_secs(5));
        }
        let dlc_state = self.get_dlc_state(name)?;
        if dlc_state.state.enum_value() != Ok(dlc_state::State::INSTALLED) {
            if dlc_state.last_error_code == DLCSERVICE_NO_IMAGE_FOUND_ERROR {
                return Err(FailedDlcInstall(
                    name.to_owned(),
                    "DLC not found for this build. Please try again after updating Chrome OS."
                        .to_string(),
                )
                .into());
            }
            return Err(
                FailedDlcInstall(name.to_owned(), "Failed to install DLC".to_string()).into(),
            );
        }
        Ok(())
    }

    fn install_dlc(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        if self.get_dlc_state(name)?.state.enum_value() != Ok(dlc_state::State::INSTALLED) {
            self.init_dlc_install(name)?;
            self.poll_dlc_install(name)?;
        }
        Ok(())
    }

    fn check_vm_type_status(
        &mut self,
        user_id_hash: &str,
        method_name: &str,
    ) -> Result<VmTypeStatus, Box<dyn Error>> {
        let method = Message::new_method_call(
            CHROME_FEATURES_SERVICE_NAME,
            CHROME_FEATURES_SERVICE_PATH,
            CHROME_FEATURES_SERVICE_INTERFACE,
            method_name,
        )?
        .append1(user_id_hash);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT)?;
        let (enabled, reason): (Option<bool>, Option<String>) = message.get2();
        match enabled {
            Some(true) => Ok(VmTypeStatus::Enabled),
            Some(false) => Ok(VmTypeStatus::Disabled(reason)),
            _ => Err(BadChromeFeatureStatus.into()),
        }
    }

    fn check_crostini_status(
        &mut self,
        user_id_hash: &str,
    ) -> Result<VmTypeStatus, Box<dyn Error>> {
        let status = match &self.crostini_enabled {
            Some(value) => value.clone(),
            None => {
                let value = self.check_vm_type_status(
                    user_id_hash,
                    CHROME_FEATURES_SERVICE_IS_CROSTINI_ENABLED_METHOD,
                )?;
                self.crostini_enabled = Some(value.clone());
                value
            }
        };
        Ok(status)
    }

    fn is_crostini_enabled(&mut self, user_id_hash: &str) -> Result<bool, Box<dyn Error>> {
        match self.check_crostini_status(user_id_hash)? {
            VmTypeStatus::Enabled => Ok(true),
            _ => Ok(false),
        }
    }

    fn ensure_crostini_available(&mut self, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        match self.check_crostini_status(user_id_hash)? {
            VmTypeStatus::Enabled => Ok(()),
            VmTypeStatus::Disabled(reason) => match reason {
                Some(r) => Err(CrostiniVmDisabledReason(r).into()),
                None => Err(CrostiniVmDisabled.into()),
            },
        }
    }

    fn check_plugin_vm_status(
        &mut self,
        user_id_hash: &str,
    ) -> Result<VmTypeStatus, Box<dyn Error>> {
        let status = match &self.plugin_vm_enabled {
            Some(value) => value.clone(),
            None => {
                let value = self.check_vm_type_status(
                    user_id_hash,
                    CHROME_FEATURES_SERVICE_IS_PLUGIN_VM_ENABLED_METHOD,
                )?;
                self.plugin_vm_enabled = Some(value.clone());
                value
            }
        };
        Ok(status)
    }

    fn is_plugin_vm_enabled(&mut self, user_id_hash: &str) -> Result<bool, Box<dyn Error>> {
        match self.check_plugin_vm_status(user_id_hash)? {
            VmTypeStatus::Enabled => Ok(true),
            _ => Ok(false),
        }
    }

    // Checks if Parallels is enabled, and starts the dispatcher.
    fn ensure_plugin_vm_available(&mut self, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        match self.check_plugin_vm_status(user_id_hash)? {
            VmTypeStatus::Enabled => self.start_vm_plugin_dispatcher(user_id_hash),
            VmTypeStatus::Disabled(reason) => match reason {
                Some(r) => Err(PluginVmDisabledReason(r).into()),
                None => Err(PluginVmDisabled.into()),
            },
        }
    }

    fn notify_vm_starting(&mut self) -> Result<(), Box<dyn Error>> {
        let method = Message::new_method_call(
            LOCK_TO_SINGLE_USER_SERVICE_NAME,
            LOCK_TO_SINGLE_USER_SERVICE_PATH,
            LOCK_TO_SINGLE_USER_INTERFACE,
            NOTIFY_VM_STARTING_METHOD,
        )?;

        self.connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT)?;

        Ok(())
    }

    /// Request debugd to start vmplugin_dispatcher.
    fn start_vm_plugin_dispatcher(&mut self, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        // Download and install pita component. If this fails we won't be able to start
        // the dispatcher service below.
        self.install_dlc("pita")?;

        let proxy: blocking::Proxy<'_, _> = Connection::with_proxy(
            self.connection.get_real_connection_or_fail()?,
            DEBUGD_SERVICE_NAME,
            DEBUGD_SERVICE_PATH,
            DEFAULT_TIMEOUT,
        );
        match OrgChromiumDebugd::start_vm_plugin_dispatcher(&proxy, user_id_hash, "en-US") {
            Ok(true) => Ok(()),
            _ => Err(BadVmPluginDispatcherStatus.into()),
        }
    }

    /// Starts all necessary VM services (currently just the Parallels dispatcher).
    fn start_vm_infrastructure(&mut self, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        if self.is_plugin_vm_enabled(user_id_hash)? {
            // Starting the dispatcher will also start concierge.
            self.start_vm_plugin_dispatcher(user_id_hash)
        } else if self.is_crostini_enabled(user_id_hash)? {
            Ok(())
        } else {
            Err(NoVmTechnologyEnabled.into())
        }
    }

    /// Request that concierge adjust an existing VM.
    fn adjust_vm(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        operation: &str,
        params: &[&str],
    ) -> Result<(), Box<dyn Error>> {
        let mut request = AdjustVmRequest::new();
        request.name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.operation = operation.to_owned();

        for param in params {
            request.params.push(param.to_string());
        }

        let response: SuccessFailureResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .adjust_vm(request.write_to_bytes()?)?,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedAdjustVm(response.failure_reason).into())
        }
    }

    /// Request that concierge create a disk image.
    fn create_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = CreateDiskImageRequest::new();
        request.vm_name = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.image_type = DiskImageType::DISK_IMAGE_AUTO.into();
        request.storage_location = StorageLocation::STORAGE_CRYPTOHOME_ROOT.into();

        let response: CreateDiskImageResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .create_disk_image(request.write_to_bytes()?, vec![])?,
        )?;

        match response.status.enum_value() {
            Ok(DiskImageStatus::DISK_STATUS_EXISTS) | Ok(DiskImageStatus::DISK_STATUS_CREATED) => {
                Ok(response.disk_path)
            }
            Ok(DiskImageStatus::DISK_STATUS_NOT_ENOUGH_SPACE) => Err(DiskImageOutOfSpace.into()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge create a new VM image.
    fn create_vm_image<T: AsRef<str>>(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        size: Option<u64>,
        source_name: Option<&str>,
        removable_media: Option<&str>,
        params: &[T],
    ) -> Result<Option<String>, Box<dyn Error>> {
        let mut request = CreateDiskImageRequest::new();
        request.vm_name = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.image_type = DiskImageType::DISK_IMAGE_AUTO.into();
        request.storage_location = if plugin_vm {
            StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM
        } else {
            StorageLocation::STORAGE_CRYPTOHOME_ROOT
        }
        .into();
        if let Some(s) = size {
            request.disk_size = s;
        }

        let source_fd = match source_name {
            Some(source) => {
                let source_file = self.open_input_file(
                    user_id_hash,
                    source,
                    removable_media,
                    OpenSafelyOptions::new().read(true),
                )?;
                request.source_size = source_file.size;
                Some(source_file.fd)
            }
            None => None,
        };

        for param in params {
            request.params.push(param.as_ref().to_string());
        }

        let owned_fds = if let Some(fd) = source_fd {
            vec![fd]
        } else {
            vec![]
        };

        let response: CreateDiskImageResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .create_disk_image(request.write_to_bytes()?, owned_fds)?,
        )?;

        match response.status.enum_value() {
            Ok(DiskImageStatus::DISK_STATUS_CREATED) => Ok(None),
            Ok(DiskImageStatus::DISK_STATUS_IN_PROGRESS) => Ok(Some(response.command_uuid)),
            Ok(DiskImageStatus::DISK_STATUS_NOT_ENOUGH_SPACE) => Err(DiskImageOutOfSpace.into()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge create a disk image.
    fn destroy_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = DestroyDiskImageRequest::new();
        request.vm_name = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();

        let response: DestroyDiskImageResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .destroy_disk_image(request.write_to_bytes()?)?,
        )?;

        match response.status.enum_value() {
            Ok(DiskImageStatus::DISK_STATUS_DESTROYED)
            | Ok(DiskImageStatus::DISK_STATUS_DOES_NOT_EXIST) => Ok(()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    fn create_output_file(
        &mut self,
        user_id_hash: &str,
        name: &str,
        removable_media: Option<&str>,
    ) -> Result<OutputFile, Box<dyn Error>> {
        let (file, path) = open_user_path(
            user_id_hash,
            name,
            removable_media,
            // Output file is always a new file, and is only accessible to the user that creates it.
            // We are not using `O_NOFOLLOW` in open flags, as `O_NOFOLLOW` only preempts symlinks
            // for the final part of the path, which is guaranteed to not exist by
            // `create_new(true)`.
            OpenSafelyOptions::new()
                .write(true)
                .read(true)
                .create_new(true)
                .mode(0o600),
        )?;

        OutputFile::new(file, path)
    }

    fn open_input_file(
        &self,
        user_id_hash: &str,
        name: &str,
        removable_media: Option<&str>,
        open_options: &OpenSafelyOptions,
    ) -> Result<InputFile, Box<dyn Error>> {
        let (file, _path) = open_user_path(user_id_hash, name, removable_media, open_options)?;
        InputFile::new(file)
    }

    /// Request that concierge export a VM's disk image.
    fn export_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        export_name: &str,
        digest_name: Option<&str>,
        removable_media: Option<&str>,
        force: bool,
    ) -> Result<Option<String>, Box<dyn Error>> {
        let mut export_file =
            self.create_output_file(user_id_hash, export_name, removable_media)?;

        let mut request = ExportDiskImageRequest::new();
        request.vm_name = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.generate_sha256_digest = digest_name.is_some();
        request.force = force;

        let mut owned_fds = vec![export_file.as_owned_fd().try_clone()?];

        let digest_file = match digest_name {
            Some(name) => {
                let digest = self.create_output_file(user_id_hash, name, removable_media)?;
                owned_fds.push(digest.as_owned_fd().try_clone()?);
                Some(digest)
            }
            None => None,
        };

        let response: ExportDiskImageResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client_with_timeout(EXPORT_DISK_TIMEOUT)?
                .export_disk_image(request.write_to_bytes()?, owned_fds)?,
        )?;

        match response.status.enum_value() {
            Ok(DiskImageStatus::DISK_STATUS_CREATED)
            | Ok(DiskImageStatus::DISK_STATUS_IN_PROGRESS) => {
                export_file.commit();
                if let Some(mut f) = digest_file {
                    f.commit();
                }
                if response.status.enum_value() == Ok(DiskImageStatus::DISK_STATUS_IN_PROGRESS) {
                    Ok(Some(response.command_uuid))
                } else {
                    Ok(None)
                }
            }
            Ok(DiskImageStatus::DISK_STATUS_NOT_ENOUGH_SPACE) => Err(DiskImageOutOfSpace.into()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge import a VM's disk image.
    fn import_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        import_name: &str,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<dyn Error>> {
        let import_file = self.open_input_file(
            user_id_hash,
            import_name,
            removable_media,
            OpenSafelyOptions::new().read(true),
        )?;

        let mut request = ImportDiskImageRequest::new();
        request.vm_name = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.storage_location = if plugin_vm {
            StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM
        } else {
            StorageLocation::STORAGE_CRYPTOHOME_ROOT
        }
        .into();
        request.source_size = import_file.size;

        let response: ImportDiskImageResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .import_disk_image(request.write_to_bytes()?, import_file.fd)?,
        )?;

        match response.status.enum_value() {
            Ok(DiskImageStatus::DISK_STATUS_CREATED) => Ok(None),
            Ok(DiskImageStatus::DISK_STATUS_IN_PROGRESS) => Ok(Some(response.command_uuid)),
            Ok(DiskImageStatus::DISK_STATUS_NOT_ENOUGH_SPACE) => Err(DiskImageOutOfSpace.into()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge resize the VM's disk.
    fn resize_disk(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        size: u64,
    ) -> Result<Option<String>, Box<dyn Error>> {
        let mut request = ResizeDiskImageRequest::new();
        request.cryptohome_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.disk_size = size;

        let response: ResizeDiskImageResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .resize_disk_image(request.write_to_bytes()?)?,
        )?;

        match response.status.enum_value() {
            Ok(DiskImageStatus::DISK_STATUS_RESIZED) => Ok(None),
            Ok(DiskImageStatus::DISK_STATUS_IN_PROGRESS) => Ok(Some(response.command_uuid)),
            Ok(DiskImageStatus::DISK_STATUS_NOT_ENOUGH_SPACE) => Err(DiskImageOutOfSpace.into()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    fn parse_disk_op_status(
        &mut self,
        response: DiskImageStatusResponse,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        let expected_status = match op_type {
            DiskOpType::Create => DiskImageStatus::DISK_STATUS_CREATED,
            DiskOpType::Resize => DiskImageStatus::DISK_STATUS_RESIZED,
        };

        if response.status.enum_value() == Ok(expected_status) {
            Ok((true, response.progress))
        } else if response.status.enum_value() == Ok(DiskImageStatus::DISK_STATUS_IN_PROGRESS) {
            Ok((false, response.progress))
        } else if response.status.enum_value() == Ok(DiskImageStatus::DISK_STATUS_NOT_ENOUGH_SPACE)
        {
            Err(DiskImageOutOfSpace.into())
        } else {
            Err(BadDiskImageStatus(response.status, response.failure_reason).into())
        }
    }

    /// Request concierge to provide status of a disk operation (import or export) with given UUID.
    fn check_disk_operation(
        &mut self,
        uuid: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        let mut request = DiskImageStatusRequest::new();
        request.command_uuid = uuid.to_owned();

        let response: DiskImageStatusResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .disk_image_status(request.write_to_bytes()?)?,
        )?;

        self.parse_disk_op_status(response, op_type)
    }

    /// Wait for updated status of a disk operation (import or export) with given UUID.
    fn wait_disk_operation(
        &mut self,
        uuid: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        loop {
            let response: DiskImageStatusResponse = self.protobus_wait_for_signal_timeout(
                VM_CONCIERGE_INTERFACE,
                DISK_IMAGE_PROGRESS_SIGNAL,
                DEFAULT_TIMEOUT,
            )?;

            if response.command_uuid == uuid {
                return self.parse_disk_op_status(response, op_type);
            }
        }
    }

    /// Request a list of disk images from concierge.
    fn list_disk_images(
        &mut self,
        user_id_hash: &str,
        target_location: Option<StorageLocation>,
        target_name: Option<&str>,
    ) -> Result<(Vec<VmDiskInfo>, u64), Box<dyn Error>> {
        let mut request = ListVmDisksRequest::new();
        request.cryptohome_id = user_id_hash.to_owned();
        match target_location {
            Some(location) => request.storage_location = location.into(),
            None => request.all_locations = true,
        };
        if let Some(vm_name) = target_name {
            request.vm_name = vm_name.to_string();
        }

        let response: ListVmDisksResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .list_vm_disks(request.write_to_bytes()?)?,
        )?;

        if response.success {
            Ok((response.images, response.total_size))
        } else {
            Err(FailedListDiskImages(response.failure_reason).into())
        }
    }

    /// Checks if VM with given name/disk is running in Parallels.
    pub fn is_plugin_vm(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<bool, Box<dyn Error>> {
        let (images, _) = self.list_disk_images(
            user_id_hash,
            Some(StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM),
            Some(vm_name),
        )?;
        Ok(!images.is_empty())
    }

    /// Gets the id of the dlc to be used to boot a VM, or None if DLC should not be used.
    fn get_dlc_id_or_none(
        &mut self,
        dlc_param: Option<String>,
        is_termina: bool,
    ) -> Result<Option<String>, Box<dyn Error>> {
        if let Some(id) = dlc_param {
            if id.is_empty() {
                // Explicitly passing the empty string means "don't use DLC".
                Ok(None)
            } else {
                Ok(Some(id))
            }
        } else if is_termina {
            Ok(Some("termina-dlc".to_owned()))
        } else {
            Ok(None)
        }
    }

    /// Request that concierge start a vm with the given disk image.
    fn start_vm_with_disk(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        username: &str,
        features: VmFeatures,
        stateful_disk_path: String,
        user_disks: UserDisks,
        start_termina: bool,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = StartVmRequest::new();
        if let Some(dlc_id) = self.get_dlc_id_or_none(features.dlc, start_termina)? {
            self.install_dlc(&dlc_id)?;
            request.vm.mut_or_insert_default().dlc_id = dlc_id;
        }

        if let Some(tools_dlc_id) = features.tools_dlc_id {
            // TODO(crbug/1276157): add `termina-tools` to this list when `termina-dlc` is split.
            match tools_dlc_id.as_ref() {
                "termina-dlc" => (),
                _ => return Err(ToolsDlcNotAllowed(tools_dlc_id.to_owned()).into()),
            }
            self.install_dlc(&tools_dlc_id)?;
            request.vm.mut_or_insert_default().tools_dlc_id = tools_dlc_id;
        }

        if let Some(bios_dlc_id) = features.bios_dlc_id {
            if user_disks.bios.is_some() {
                return Err(BiosAlreadySpecified(bios_dlc_id.to_owned()).into());
            }

            match bios_dlc_id.as_ref() {
                "edk2-ovmf-dlc" => (),
                _ => return Err(BiosDlcNotAllowed(bios_dlc_id.to_owned()).into()),
            }
            self.install_dlc(&bios_dlc_id)?;
            request.vm.mut_or_insert_default().bios_dlc_id = bios_dlc_id;
        }

        if let Some(vm_type) = features.vm_type {
            request.vm_type = vm_type.into();
        };
        request.start_termina = start_termina;
        request.owner_id = user_id_hash.to_owned();
        request.vm_username = username.to_owned();
        request.enable_gpu = features.gpu;
        request.enable_dgpu_passthrough = features.dgpu_passthrough;
        request.enable_big_gl = features.big_gl;
        request.enable_virtgpu_native_context = features.virtgpu_native_context;
        request.vtpm_proxy = features.vtpm_proxy;
        request.enable_audio_capture = features.audio_capture;
        request.name = vm_name.to_owned();
        request.kernel_params = features.kernel_params;
        request.timeout = features.timeout;
        request.oem_strings = features.oem_strings;
        request.disks.push(DiskImage {
            path: stateful_disk_path,
            writable: true,
            do_mount: false,
            ..Default::default()
        });
        let tremplin_started = ProtobusSignalWatcher::new(
            self.connection.connection.as_ref(),
            VM_CICERONE_INTERFACE,
            TREMPLIN_STARTED_SIGNAL,
        )?;

        let mut owned_fds = Vec::new();
        // User-specified kernel
        if let Some(path) = user_disks.kernel {
            let file = self.open_input_file(
                user_id_hash,
                &path,
                None,
                OpenSafelyOptions::new()
                    .read(true)
                    .custom_flags(libc::O_NOFOLLOW),
            )?;
            request.fds.push(start_vm_request::FdType::KERNEL.into());
            owned_fds.push(file.fd);
        }

        // User-specified rootfs
        if let Some(path) = user_disks.rootfs {
            let file = self.open_input_file(
                user_id_hash,
                &path,
                None,
                OpenSafelyOptions::new()
                    .read(true)
                    .write(user_disks.writable_rootfs)
                    .custom_flags(libc::O_NOFOLLOW),
            )?;
            request.fds.push(start_vm_request::FdType::ROOTFS.into());
            request.writable_rootfs = user_disks.writable_rootfs;
            owned_fds.push(file.fd);
        }

        // User-specified extra disk
        if let Some(path) = user_disks.extra_disk {
            let file = self.open_input_file(
                user_id_hash,
                &path,
                None,
                OpenSafelyOptions::new()
                    .read(true)
                    .write(true) // extra disk is writable
                    .custom_flags(libc::O_NOFOLLOW),
            )?;
            request.fds.push(start_vm_request::FdType::STORAGE.into());
            owned_fds.push(file.fd);
        }

        // User-specified initrd
        if let Some(path) = user_disks.initrd {
            let file = self.open_input_file(
                user_id_hash,
                &path,
                None,
                OpenSafelyOptions::new()
                    .read(true)
                    .custom_flags(libc::O_NOFOLLOW),
            )?;
            request.fds.push(start_vm_request::FdType::INITRD.into());
            owned_fds.push(file.fd);
        }

        // User-specified bios.
        if let Some(path) = user_disks.bios {
            let file = self.open_input_file(
                user_id_hash,
                &path,
                None,
                OpenSafelyOptions::new()
                    .read(true)
                    .custom_flags(libc::O_NOFOLLOW),
            )?;
            request.fds.push(start_vm_request::FdType::BIOS.into());
            owned_fds.push(file.fd);
        }

        // User-specified pflash.
        if let Some(path) = user_disks.pflash {
            let file = self.open_input_file(
                user_id_hash,
                &path,
                None,
                OpenSafelyOptions::new()
                    .read(true)
                    .write(true)
                    .custom_flags(libc::O_NOFOLLOW),
            )?;
            request.fds.push(start_vm_request::FdType::PFLASH.into());
            owned_fds.push(file.fd);
        }

        let tremplin_timeout = if features.timeout == 0 {
            DEFAULT_TIMEOUT
        } else {
            Duration::from_secs(features.timeout.into())
        };

        // Send a protobuf request with the FDs.
        let response: StartVmResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client_with_timeout(tremplin_timeout)?
                .start_vm(request.write_to_bytes()?, owned_fds)?,
        )?;

        match response.status.enum_value() {
            Ok(VmStatus::VM_STATUS_STARTING) => {
                assert!(response.success);
                if start_termina {
                    tremplin_started.wait_with_filter(
                        tremplin_timeout,
                        |s: &TremplinStartedSignal| {
                            s.vm_name == vm_name && s.owner_id == user_id_hash
                        },
                    )?;
                }
                Ok(())
            }
            Ok(VmStatus::VM_STATUS_RUNNING) => {
                assert!(response.success);
                Ok(())
            }
            _ => Err(BadVmStatus(response.status, response.failure_reason).into()),
        }
    }

    fn start_lxd(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = StartLxdRequest::new();
        request.vm_name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();

        let lxd_started = ProtobusSignalWatcher::new(
            self.connection.connection.as_ref(),
            VM_CICERONE_INTERFACE,
            START_LXD_PROGRESS_SIGNAL,
        )?;

        let proxy = cicerone(
            self.connection.get_real_connection_or_fail()?,
            DEFAULT_TIMEOUT,
        );

        let response: StartLxdResponse = ProtoMessage::parse_from_bytes(
            &OrgChromiumVmCicerone::start_lxd(&proxy, request.write_to_bytes()?)?,
        )?;

        use self::start_lxd_response::Status::*;
        match response.status.enum_value() {
            Ok(STARTING) => {
                use self::start_lxd_progress_signal::Status::*;
                let signal = lxd_started.wait_with_filter(
                    DEFAULT_TIMEOUT,
                    |s: &StartLxdProgressSignal| {
                        s.vm_name == vm_name
                            && s.owner_id == user_id_hash
                            && s.status != STARTING.into()
                            && s.status != RECOVERING.into()
                    },
                )?;
                match signal.status.enum_value() {
                    Ok(STARTED) => Ok(()),
                    Ok(STARTING) | Ok(RECOVERING) => unreachable!(),
                    Ok(UNKNOWN) | Ok(FAILED) | Err(_) => Err(FailedStartLxdProgressSignal(
                        signal.status,
                        signal.failure_reason,
                    )
                    .into()),
                }
            }
            Ok(ALREADY_RUNNING) => Ok(()),
            Ok(UNKNOWN) | Ok(FAILED) | Err(_) => {
                Err(FailedStartLxdStatus(response.status, response.failure_reason).into())
            }
        }
    }

    fn parse_plugin_vm_response(
        &mut self,
        error: EnumOrUnknown<VmErrorCode>,
        result_code: i32,
    ) -> Result<(), Box<dyn Error>> {
        const PRL_ERR_SUCCESS: u32 = 0;
        const PRL_ERR_DISP_SHUTDOWN_IN_PROCESS: u32 = 0x80000404;
        const PRL_ERR_NOT_ENOUGH_DISK_SPACE_TO_START_VM: u32 = 0x80000456;
        const PRL_ERR_LICENSE_NOT_VALID: u32 = 0x80011000;
        const PRL_ERR_LICENSE_EXPIRED: u32 = 0x80011001;
        const PRL_ERR_LICENSE_WRONG_VERSION: u32 = 0x80011002;
        const PRL_ERR_LICENSE_WRONG_PLATFORM: u32 = 0x80011004;
        const PRL_ERR_LICENSE_BETA_KEY_RELEASE_PRODUCT: u32 = 0x80011011;
        const PRL_ERR_LICENSE_RELEASE_KEY_BETA_PRODUCT: u32 = 0x80011013;
        const PRL_ERR_LICENSE_SUBSCR_EXPIRED: u32 = 0x80011074;
        const PRL_ERR_JLIC_WRONG_HWID: u32 = 0x80057005;
        const PRL_ERR_JLIC_LICENSE_DISABLED: u32 = 0x80057010;
        const PRL_ERR_JLIC_WEB_PORTAL_ACCESS_REQUIRED: u32 = 0x80057012;

        match error.enum_value() {
            Ok(VmErrorCode::VM_SUCCESS) => Ok(()),
            Ok(VmErrorCode::VM_ERR_NATIVE_RESULT_CODE) | Err(_) => match result_code as u32 {
                PRL_ERR_SUCCESS => Ok(()),
                PRL_ERR_DISP_SHUTDOWN_IN_PROCESS => Err(PluginVmGenericError(result_code).into()),
                PRL_ERR_NOT_ENOUGH_DISK_SPACE_TO_START_VM => Err(PluginVmNotEnoughDisk.into()),
                PRL_ERR_LICENSE_NOT_VALID
                | PRL_ERR_LICENSE_WRONG_VERSION
                | PRL_ERR_LICENSE_WRONG_PLATFORM
                | PRL_ERR_LICENSE_BETA_KEY_RELEASE_PRODUCT
                | PRL_ERR_LICENSE_RELEASE_KEY_BETA_PRODUCT
                | PRL_ERR_JLIC_WRONG_HWID
                | PRL_ERR_JLIC_LICENSE_DISABLED => Err(PluginVmLicenseInvalid(result_code).into()),
                PRL_ERR_LICENSE_EXPIRED | PRL_ERR_LICENSE_SUBSCR_EXPIRED => {
                    Err(PluginVmLicenseExpired(result_code).into())
                }
                PRL_ERR_JLIC_WEB_PORTAL_ACCESS_REQUIRED => Err(PluginVmNoPortalAccess.into()),
                _ => Err(PluginVmGenericError(result_code).into()),
            },
        }
    }

    /// Request that dispatcher start given Parallels VM.
    fn start_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = vm_plugin_dispatcher::StartVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name_uuid = vm_name.to_owned();

        let response: vm_plugin_dispatcher::StartVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                START_VM_METHOD,
            )?,
            &request,
        )?;

        self.parse_plugin_vm_response(response.error, response.result_code)
    }

    /// Request that dispatcher starts application responsible for rendering Parallels VM window.
    fn show_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = vm_plugin_dispatcher::ShowVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name_uuid = vm_name.to_owned();

        let response: vm_plugin_dispatcher::ShowVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                SHOW_VM_METHOD,
            )?,
            &request,
        )?;

        self.parse_plugin_vm_response(response.error, response.result_code)
    }

    /// Request that `concierge` stop a vm with the given disk image.
    fn stop_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = StopVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.name = vm_name.to_owned();

        let response: SuccessFailureResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .stop_vm(request.write_to_bytes()?)?,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedStopVm {
                vm_name: vm_name.to_owned(),
                reason: response.failure_reason,
            }
            .into())
        }
    }

    // Request `VmInfo` from concierge about given `vm_name`.
    fn get_vm_info(&mut self, vm_name: &str, user_id_hash: &str) -> Result<VmInfo, Box<dyn Error>> {
        let mut request = GetVmInfoRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.name = vm_name.to_owned();

        let response: GetVmInfoResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .get_vm_info(request.write_to_bytes()?)?,
        )?;

        if response.success {
            Ok(response.vm_info.unwrap_or_default())
        } else {
            Err(FailedGetVmInfo.into())
        }
    }

    // Request the given `path` be shared with the seneschal instance associated with the desired
    // vm, owned by `user_id_hash`.
    fn share_path_with_vm(
        &mut self,
        seneschal_handle: u32,
        user_id_hash: &str,
        path: &str,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = SharePathRequest::new();
        request.handle = seneschal_handle;
        request.shared_path.mut_or_insert_default().path = path.to_owned();
        request.storage_location = share_path_request::StorageLocation::MY_FILES.into();
        request.owner_id = user_id_hash.to_owned();

        let response: SharePathResponse = self.sync_protobus(
            Message::new_method_call(
                SENESCHAL_SERVICE_NAME,
                SENESCHAL_SERVICE_PATH,
                SENESCHAL_INTERFACE,
                SHARE_PATH_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(response.path)
        } else {
            Err(FailedSharePath(response.failure_reason).into())
        }
    }

    // Request the given `path` be no longer shared with the vm associated with given seneshal
    // instance.
    fn unshare_path_with_vm(
        &mut self,
        seneschal_handle: u32,
        path: &str,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = UnsharePathRequest::new();
        request.handle = seneschal_handle;
        request.path = format!("MyFiles/{}", path);

        let response: SharePathResponse = self.sync_protobus(
            Message::new_method_call(
                SENESCHAL_SERVICE_NAME,
                SENESCHAL_SERVICE_PATH,
                SENESCHAL_INTERFACE,
                UNSHARE_PATH_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedSharePath(response.failure_reason).into())
        }
    }

    fn create_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        source: ContainerSource,
        timeout: Option<i32>,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = CreateLxdContainerRequest::new();
        request.vm_name = vm_name.to_owned();
        request.container_name = container_name.to_owned();
        request.owner_id = user_id_hash.to_owned();

        let timeout = timeout
            .map(|x| Duration::from_secs(x.try_into().expect("timeout should not be negative")))
            .unwrap_or(DEFAULT_TIMEOUT);
        match source {
            ContainerSource::ImageServer {
                image_alias,
                image_server,
            } => {
                request.image_server = image_server;
                request.image_alias = image_alias;
            }
            ContainerSource::Tarballs {
                rootfs_path,
                metadata_path,
            } => {
                request.rootfs_path = rootfs_path;
                request.metadata_path = metadata_path;
            }
        }

        let proxy = cicerone(
            self.connection.get_real_connection_or_fail()?,
            DEFAULT_TIMEOUT,
        );
        let response: CreateLxdContainerResponse = ProtoMessage::parse_from_bytes(
            &OrgChromiumVmCicerone::create_lxd_container(&proxy, request.write_to_bytes()?)?,
        )?;

        use self::create_lxd_container_response::Status::*;
        use self::lxd_container_created_signal::Status::*;
        match response.status.enum_value() {
            Ok(CREATING) => {
                let signal: LxdContainerCreatedSignal = self.protobus_wait_for_signal_timeout(
                    VM_CICERONE_INTERFACE,
                    LXD_CONTAINER_CREATED_SIGNAL,
                    timeout,
                )?;
                match signal.status.enum_value() {
                    Ok(CREATED) => Ok(()),
                    _ => Err(
                        FailedCreateContainerSignal(signal.status, signal.failure_reason).into(),
                    ),
                }
            }
            Ok(EXISTS) => Ok(()),
            _ => Err(FailedCreateContainer(response.status, response.failure_reason).into()),
        }
    }

    fn start_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        privilege_level: start_lxd_container_request::PrivilegeLevel,
        timeout: Option<i32>,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = StartLxdContainerRequest::new();
        request.vm_name = vm_name.to_owned();
        request.container_name = container_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.privilege_level = privilege_level.into();

        let timeout = timeout
            .map(|x| Duration::from_secs(x.try_into().expect("timeout should not be negative")))
            .unwrap_or(DEFAULT_TIMEOUT);

        let proxy = cicerone(
            self.connection.get_real_connection_or_fail()?,
            DEFAULT_TIMEOUT,
        );
        let response: StartLxdContainerResponse = ProtoMessage::parse_from_bytes(
            &OrgChromiumVmCicerone::start_lxd_container(&proxy, request.write_to_bytes()?)?,
        )?;

        use system_api::cicerone_service::start_lxd_container_response::Status::*;
        match response.status.enum_value() {
            // |REMAPPING| happens when the privilege level of a container was changed before this
            // boot. It's a long running operation and when it happens it's returned in lieu of
            // |STARTING|. It makes sense to treat them the same way.
            Ok(STARTING) | Ok(REMAPPING) => {
                use self::lxd_container_starting_signal::Status::*;
                let container_started = ProtobusSignalWatcher::new(
                    self.connection.connection.as_ref(),
                    VM_CICERONE_INTERFACE,
                    LXD_CONTAINER_STARTING_SIGNAL,
                )?;
                let signal = container_started.wait_with_filter(
                    timeout,
                    |s: &LxdContainerStartingSignal| {
                        s.vm_name == vm_name
                            && s.owner_id == user_id_hash
                            && s.container_name == container_name
                            && s.status != STARTING.into()
                    },
                )?;
                match signal.status.enum_value() {
                    Ok(STARTED) => Ok(()),
                    _ => {
                        Err(FailedLxdContainerStarting(signal.status, signal.failure_reason).into())
                    }
                }
            }
            Ok(RUNNING) => Ok(()),
            _ => Err(FailedStartContainerStatus(response.status, response.failure_reason).into()),
        }
    }

    fn setup_vm_user(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        user_info: UserInfo,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = SetUpVmUserRequest::new();
        request.vm_name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.username = user_info.username;
        request.uid = user_info.uid;
        for gname in user_info.group_names {
            request.group_names.push(gname);
        }

        let response: SetUpVmUserResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .set_up_vm_user(request.write_to_bytes()?)?,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedSetUpVmUser(response.failure_reason).into())
        }
    }

    fn setup_container_user(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        username: &str,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = SetUpLxdContainerUserRequest::new();
        request.vm_name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.container_name = container_name.to_owned();
        request.container_username = username.to_owned();

        let proxy = cicerone(
            self.connection.get_real_connection_or_fail()?,
            DEFAULT_TIMEOUT,
        );

        let response: SetUpLxdContainerUserResponse = ProtoMessage::parse_from_bytes(
            &OrgChromiumVmCicerone::set_up_lxd_container_user(&proxy, request.write_to_bytes()?)?,
        )?;

        use self::set_up_lxd_container_user_response::Status::*;
        match response.status.enum_value() {
            Ok(SUCCESS) | Ok(EXISTS) => Ok(()),
            _ => Err(FailedSetupContainerUser(response.status, response.failure_reason).into()),
        }
    }

    fn update_container_devices(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        updates: &HashMap<String, VmDeviceAction>,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = UpdateContainerDevicesRequest::new();
        request.vm_name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.container_name = container_name.to_owned();
        request.updates = HashMap::from_iter(updates.iter().map(|(k, v)| (k.clone(), (*v).into())));

        let proxy = cicerone(
            self.connection.get_real_connection_or_fail()?,
            DEFAULT_TIMEOUT,
        );
        let response: UpdateContainerDevicesResponse = ProtoMessage::parse_from_bytes(
            &OrgChromiumVmCicerone::update_container_devices(&proxy, request.write_to_bytes()?)?,
        )?;

        use self::update_container_devices_response::Status::*;
        match response.status.enum_value() {
            Ok(OK) => Ok(format!("Results: {:?}", response.results)),
            _ => Err(FailedUpdateContainerDevices(format!(
                "{} . Results: {:?}",
                response.failure_reason, response.results
            ))
            .into()),
        }
    }

    fn attach_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        bus: u8,
        device: u8,
        usb_fd: OwnedFd,
        container_name: Option<&str>,
    ) -> Result<u8, Box<dyn Error>> {
        let mut request = AttachUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.bus_number = bus as u32;
        request.port_number = device as u32;

        let response: AttachUsbDeviceResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .attach_usb_device(request.write_to_bytes()?, usb_fd)?,
        )?;

        if !response.success {
            return Err(FailedAttachUsb(response.reason).into());
        }

        let guest_port: u8 = response.guest_port as u8;

        if let Some(container) = container_name {
            let mut request = AttachUsbToContainerRequest::new();
            request.owner_id = user_id_hash.to_owned();
            request.container_name = container.to_owned();
            request.vm_name = vm_name.to_owned();
            request.port_num = guest_port as i32;

            let proxy = cicerone(
                self.connection.get_real_connection_or_fail()?,
                DEFAULT_TIMEOUT,
            );

            let response: AttachUsbToContainerResponse = ProtoMessage::parse_from_bytes(
                &OrgChromiumVmCicerone::attach_usb_to_container(&proxy, request.write_to_bytes()?)?,
            )?;

            match response.status.enum_value() {
                Ok(attach_usb_to_container_response::Status::OK) => Ok(guest_port),
                _ => {
                    Err(FailedAttachUsbToContainer(response.status, response.failure_reason).into())
                }
            }
        } else {
            Ok(guest_port)
        }
    }

    fn attach_key(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        hidraw_fd: OwnedFd,
    ) -> Result<u8, Box<dyn Error>> {
        let mut request = AttachKeyRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();

        let response: AttachKeyResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .attach_key(request.write_to_bytes()?, hidraw_fd)?,
        )?;

        if !response.success {
            return Err(FailedAttachKey(response.reason).into());
        }

        let guest_port: u8 = response.guest_port as u8;

        // TODO(morg): need to implement sharing security key with container
        Ok(guest_port)
    }

    fn detach_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        port: u8,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = DetachUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.guest_port = port as u32;

        let response: SuccessFailureResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .detach_usb_device(request.write_to_bytes()?)?,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedDetachUsb(response.failure_reason).into())
        }
    }

    fn list_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<Vec<UsbDeviceMessage>, Box<dyn Error>> {
        let mut request = ListUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();

        let response: ListUsbDeviceResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .list_usb_devices(request.write_to_bytes()?)?,
        )?;

        if response.success {
            Ok(response.usb_devices)
        } else {
            Err(FailedListUsb.into())
        }
    }

    fn permission_broker_open_path(&mut self, path: &Path) -> Result<OwnedFd, Box<dyn Error>> {
        let path_str = path
            .to_str()
            .ok_or_else(|| FailedGetOpenPath(path.into()))?;

        let proxy: blocking::Proxy<'_, _> = Connection::with_proxy(
            self.connection.get_real_connection_or_fail()?,
            PERMISSION_BROKER_SERVICE_NAME,
            PERMISSION_BROKER_SERVICE_PATH,
            DEFAULT_TIMEOUT,
        );

        OrgChromiumPermissionBroker::open_path(&proxy, path_str)
            .map_err(|_| FailedGetOpenPath(path.into()).into())
    }

    pub fn list_primary_io_devices(&mut self) -> Result<Vec<String>, Box<dyn Error>> {
        let method = Message::new_method_call(
            PRIMARY_IO_MANAGER_SERVICE_NAME,
            PRIMARY_IO_MANAGER_SERVICE_PATH,
            PRIMARY_IO_MANAGER_INTERFACE,
            GET_IO_DEVICES,
        )?;

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT)?;

        match message.get1::<Vec<String>>() {
            Some(devices) => Ok(devices),
            _ => Err(GetIoDevices.into()),
        }
    }

    pub fn unset_primary_keyboard(&self) -> Result<(), Box<dyn Error>> {
        let method = Message::new_method_call(
            PRIMARY_IO_MANAGER_SERVICE_NAME,
            PRIMARY_IO_MANAGER_SERVICE_PATH,
            PRIMARY_IO_MANAGER_INTERFACE,
            UNSET_PRIMARY_KEYBOARD,
        )?;

        self.connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT)?;

        Ok(())
    }

    pub fn unset_primary_mouse(&self) -> Result<(), Box<dyn Error>> {
        let method = Message::new_method_call(
            PRIMARY_IO_MANAGER_SERVICE_NAME,
            PRIMARY_IO_MANAGER_SERVICE_PATH,
            PRIMARY_IO_MANAGER_INTERFACE,
            UNSET_PRIMARY_MOUSE,
        )?;

        self.connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT)?;

        Ok(())
    }

    fn send_problem_report_for_plugin_vm(
        &mut self,
        vm_name: Option<String>,
        user_id_hash: &str,
        email: Option<String>,
        text: Option<String>,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = vm_plugin_dispatcher::SendProblemReportRequest::new();
        request.owner_id = user_id_hash.to_owned();

        if let Some(name) = vm_name {
            let (images, _) = self.list_disk_images(user_id_hash, None, Some(&name))?;
            if images.is_empty() {
                return Err(NoSuchVm.into());
            }
            if images[0].image_type != DiskImageType::DISK_IMAGE_PLUGINVM.into() {
                return Err(NotPluginVm.into());
            }
            request.vm_name_uuid = name;
        }

        if !self.is_plugin_vm_enabled(user_id_hash)? {
            return Err(PluginVmDisabled.into());
        }

        request.detailed = true;

        if let Some(email_str) = email {
            request.email = email_str;
        }

        if let Some(text_str) = text {
            request.description = text_str;
        }

        let response: vm_plugin_dispatcher::SendProblemReportResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                SEND_PROBLEM_REPORT_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(response.report_id)
        } else {
            Err(FailedSendProblemReport(response.error_message, response.result_code).into())
        }
    }

    pub fn metrics_send_sample(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        #![allow(unreachable_code)]
        let _ = name;
        // Metrics are not appropriate for test builds
        #[cfg(test)]
        return Ok(());

        let status = Command::new("metrics_client")
            .arg("-v")
            .arg(name)
            .status()?;
        if !status.success() {
            return Err(FailedMetricsSend {
                exit_code: status.code(),
            }
            .into());
        }
        Ok(())
    }

    pub fn sessions_list(&mut self) -> Result<Vec<(String, String)>, Box<dyn Error>> {
        let proxy: blocking::Proxy<'_, _> = Connection::with_proxy(
            self.connection.get_real_connection_or_fail()?,
            SESSION_MANAGER_SERVICE_NAME,
            SESSION_MANAGER_SERVICE_PATH,
            DEFAULT_TIMEOUT,
        );

        match OrgChromiumSessionManagerInterface::retrieve_active_sessions(&proxy) {
            Ok(sessions) => Ok(sessions.into_iter().collect()),
            Err(_) => Err(RetrieveActiveSessions.into()),
        }
    }

    pub fn user_id_hash_to_username(
        &mut self,
        user_id_hash: &str,
    ) -> Result<String, Box<dyn Error>> {
        let sessions = self.sessions_list()?;
        let email = sessions
            .iter()
            .find(|(_, hash)| hash == &user_id_hash)
            .map(|(email, _)| email)
            .ok_or(MissingActiveSession)?;

        match email.find('@') {
            Some(0) | None => Err(InvalidEmail.into()),
            Some(end) => Ok(email[..end].into()),
        }
    }

    pub fn vm_create<T: AsRef<str>>(
        &mut self,
        name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        size: Option<u64>,
        source_name: Option<&str>,
        removable_media: Option<&str>,
        params: &[T],
    ) -> Result<Option<String>, Box<dyn Error>> {
        if plugin_vm {
            self.ensure_plugin_vm_available(user_id_hash)?;
        } else {
            self.ensure_crostini_available(user_id_hash)?;
        }
        self.create_vm_image(
            name,
            user_id_hash,
            plugin_vm,
            size,
            source_name,
            removable_media,
            params,
        )
    }

    pub fn vm_adjust(
        &mut self,
        name: &str,
        user_id_hash: &str,
        operation: &str,
        params: &[&str],
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.adjust_vm(name, user_id_hash, operation, params)
    }

    pub fn vm_start(
        &mut self,
        name: &str,
        user_id_hash: &str,
        username: &str,
        features: VmFeatures,
        user_disks: UserDisks,
        start_lxd: bool,
        user_info: Option<UserInfo>,
    ) -> Result<(), Box<dyn Error>> {
        if self.is_plugin_vm(name, user_id_hash)? {
            self.ensure_plugin_vm_available(user_id_hash)?;
            self.notify_vm_starting()?;
            self.start_plugin_vm(name, user_id_hash)
        } else {
            self.ensure_crostini_available(user_id_hash)?;

            let disk_image_path = self.create_disk_image(name, user_id_hash)?;
            self.notify_vm_starting()?;
            self.start_vm_with_disk(
                name,
                user_id_hash,
                username,
                features,
                disk_image_path,
                user_disks,
                start_lxd,
            )?;
            if start_lxd {
                self.start_lxd(name, user_id_hash)?;
            }
            if let Some(info) = user_info {
                self.setup_vm_user(name, user_id_hash, info)?;
            }
            Ok(())
        }
    }

    pub fn vm_stop(&mut self, name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.stop_vm(name, user_id_hash)
    }

    pub fn vm_launch(
        &mut self,
        user_id_hash: &str,
        descriptors: &[&str],
    ) -> Result<(), Box<dyn Error>> {
        let mut request = EnsureVmLaunchedRequest::new();
        request.owner_id = user_id_hash.to_owned();
        // The below validation is intentionally conservative, no more than 5x 32-character
        // descriptors matching [a-zA-Z0-9_-]*.
        request.launch_descriptors = descriptors
            .iter()
            .map(|d| {
                d.chars()
                    .filter(|c| match c {
                        '_' => true,
                        '-' => true,
                        other => other.is_ascii_alphanumeric(),
                    })
                    .take(32)
                    .collect::<String>()
            })
            .take(5)
            .collect::<Vec<_>>();

        let response: EnsureVmLaunchedResponse = self.sync_protobus(
            Message::new_method_call(
                VM_LAUNCH_SERVICE_NAME,
                VM_LAUNCH_SERVICE_PATH,
                VM_LAUNCH_SERVICE_INTERFACE,
                VM_LAUNCH_SERVICE_ENSURE_VM_LAUNCHED_METHOD,
            )?,
            &request,
        )?;

        match response.container_name.as_str() {
            "" => self.vsh_exec(&response.vm_name, user_id_hash),
            container => self.vsh_exec_container(&response.vm_name, user_id_hash, container),
        }
    }

    pub fn vm_export(
        &mut self,
        name: &str,
        user_id_hash: &str,
        file_name: &str,
        digest_name: Option<&str>,
        removable_media: Option<&str>,
        force: bool,
    ) -> Result<Option<String>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.export_disk_image(
            name,
            user_id_hash,
            file_name,
            digest_name,
            removable_media,
            force,
        )
    }

    pub fn vm_import(
        &mut self,
        name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        file_name: &str,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<dyn Error>> {
        if plugin_vm {
            self.ensure_plugin_vm_available(user_id_hash)?;
        } else {
            self.ensure_crostini_available(user_id_hash)?;
        }
        self.import_disk_image(name, user_id_hash, plugin_vm, file_name, removable_media)
    }

    pub fn vm_share_path(
        &mut self,
        name: &str,
        user_id_hash: &str,
        path: &str,
    ) -> Result<String, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let vm_info = self.get_vm_info(name, user_id_hash)?;
        let vm_path =
            self.share_path_with_vm(vm_info.seneschal_server_handle, user_id_hash, path)?;
        Ok(format!("{}/{}", MNT_SHARED_ROOT, vm_path))
    }

    pub fn vm_unshare_path(
        &mut self,
        name: &str,
        user_id_hash: &str,
        path: &str,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let vm_info = self.get_vm_info(name, user_id_hash)?;
        self.unshare_path_with_vm(vm_info.seneschal_server_handle.try_into()?, path)
    }

    pub fn vsh_exec(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            self.show_plugin_vm(vm_name, user_id_hash)
        } else {
            Command::new("vsh")
                .arg(format!("--vm_name={}", vm_name))
                .arg(format!("--owner_id={}", user_id_hash))
                .args([
                    "--",
                    "LXD_DIR=/mnt/stateful/lxd",
                    "LXD_CONF=/mnt/stateful/lxd_conf",
                ])
                .status()?;
            Ok(())
        }
    }

    pub fn vsh_exec_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
    ) -> Result<(), Box<dyn Error>> {
        Command::new("vsh")
            .arg(format!("--vm_name={}", vm_name))
            .arg(format!("--owner_id={}", user_id_hash))
            .arg(format!("--target_container={}", container_name))
            .args([
                "--",
                "LXD_DIR=/mnt/stateful/lxd",
                "LXD_CONF=/mnt/stateful/lxd_conf",
            ])
            .status()?;

        Ok(())
    }

    pub fn disk_destroy(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.destroy_disk_image(vm_name, user_id_hash)
    }

    pub fn disk_list(
        &mut self,
        user_id_hash: &str,
    ) -> Result<(Vec<DiskInfo>, u64), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let (images, total_size) = self.list_disk_images(user_id_hash, None, None)?;
        let out_images: Vec<DiskInfo> = images
            .into_iter()
            .map(|e| -> Result<DiskInfo, Box<dyn Error>> {
                let state = match self.get_vm_info(&e.name, user_id_hash) {
                    Ok(vm_info) => match vm_info.status.enum_value() {
                        Ok(VmStatus::VM_STATUS_STARTING) => VmState::Starting,
                        Ok(VmStatus::VM_STATUS_RUNNING) => VmState::Running,
                        _ => VmState::Stopped,
                    },
                    Err(e) => match e.downcast::<ChromeOSError>() {
                        Ok(e) => match *e {
                            FailedGetVmInfo => VmState::Stopped,
                            _ => return Err(e),
                        },
                        Err(e) => return Err(e),
                    },
                };
                let info = DiskInfo {
                    name: e.name,
                    size: e.size,
                    min_size: if e.min_size != 0 {
                        Some(e.min_size)
                    } else {
                        None
                    },
                    image_type: match e.image_type.enum_value_or_default() {
                        DiskImageType::DISK_IMAGE_RAW => VmDiskImageType::Raw,
                        DiskImageType::DISK_IMAGE_QCOW2 => VmDiskImageType::Qcow2,
                        DiskImageType::DISK_IMAGE_AUTO => VmDiskImageType::Auto,
                        DiskImageType::DISK_IMAGE_PLUGINVM => VmDiskImageType::PluginVm,
                    },
                    user_chosen_size: e.user_chosen_size,
                    state,
                };
                Ok(info)
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok((out_images, total_size))
    }

    pub fn disk_resize(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        size: u64,
    ) -> Result<Option<String>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.resize_disk(vm_name, user_id_hash, size)
    }

    pub fn disk_op_status(
        &mut self,
        uuid: &str,
        user_id_hash: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.check_disk_operation(uuid, op_type)
    }

    pub fn wait_disk_op(
        &mut self,
        uuid: &str,
        user_id_hash: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.wait_disk_operation(uuid, op_type)
    }

    pub fn extra_disk_create(
        &mut self,
        user_id_hash: &str,
        file_name: &str,
        removable_media: Option<&str>,
        disk_size: u64,
    ) -> Result<PathBuf, Box<dyn Error>> {
        // Validate `disk_size`.
        let disk_size =
            libc::off64_t::try_from(disk_size).map_err(|_| InvalidDiskSize(disk_size))?;

        let mut output_file = self.create_output_file(user_id_hash, file_name, removable_media)?;

        // Truncate a disk file.
        // Safe since we pass in a valid fd and disk_size.
        let ret = unsafe { libc::posix_fallocate64(output_file.fd.as_raw_fd(), 0, disk_size) };
        if ret != 0 {
            let reason = format!("{}", std::io::Error::from_raw_os_error(ret));
            output_file.remove();
            return Err(FailedAllocateExtraDisk {
                path: output_file.path.clone(),
                reason,
            }
            .into());
        }
        output_file.commit();
        Ok(output_file.path.clone())
    }

    pub fn container_create(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        source: ContainerSource,
        timeout: Option<i32>,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.create_container(vm_name, user_id_hash, container_name, source, timeout)
    }

    pub fn container_start(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        privilege_level: start_lxd_container_request::PrivilegeLevel,
        timeout: Option<i32>,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.start_container(
            vm_name,
            user_id_hash,
            container_name,
            privilege_level,
            timeout,
        )
    }

    pub fn container_setup_user(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        username: &str,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.setup_container_user(vm_name, user_id_hash, container_name, username)
    }

    pub fn container_update_devices(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        updates: &HashMap<String, VmDeviceAction>,
    ) -> Result<String, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.update_container_devices(vm_name, user_id_hash, container_name, updates)
    }

    pub fn usb_attach(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        bus: u8,
        device: u8,
        container_name: Option<&str>,
    ) -> Result<u8, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let usb_file_path = format!("/dev/bus/usb/{:03}/{:03}", bus, device);
        let usb_fd = self.permission_broker_open_path(Path::new(&usb_file_path))?;
        self.attach_usb(vm_name, user_id_hash, bus, device, usb_fd, container_name)
    }

    pub fn key_attach(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        hidraw_device: &str,
    ) -> Result<u8, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let hidraw_fd = self.permission_broker_open_path(Path::new(hidraw_device))?;
        self.attach_key(vm_name, user_id_hash, hidraw_fd)
    }

    pub fn usb_detach(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        port: u8,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.detach_usb(vm_name, user_id_hash, port)
    }

    pub fn usb_list(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<Vec<(u8, u16, u16, String)>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let device_list = self
            .list_usb(vm_name, user_id_hash)?
            .into_iter()
            .map(|d| {
                (
                    d.guest_port as u8,
                    d.vendor_id as u16,
                    d.product_id as u16,
                    d.device_name,
                )
            })
            .collect();
        Ok(device_list)
    }

    pub fn pvm_send_problem_report(
        &mut self,
        vm_name: Option<String>,
        user_id_hash: &str,
        email: Option<String>,
        text: Option<String>,
    ) -> Result<String, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.send_problem_report_for_plugin_vm(vm_name, user_id_hash, email, text)
    }

    pub fn get_vm_logs(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = GetVmLogsRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.name = vm_name.to_owned();

        let response: GetVmLogsResponse = ProtoMessage::parse_from_bytes(
            &self
                .concierge_client()?
                .get_vm_logs(request.write_to_bytes()?)?,
        )?;

        Ok(response.log)
    }
}
