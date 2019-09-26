// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::collections::HashMap;
use std::error::Error;
use std::fmt;
use std::fs::OpenOptions;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::IntoRawFd;
use std::path::{Component, Path, PathBuf};
use std::process::Command;

use dbus::{BusType, Connection, ConnectionItem, Message, OwnedFd};
use protobuf::Message as ProtoMessage;

use backends::{Backend, VmFeatures};
use lsb_release::{LsbRelease, ReleaseChannel};
use proto::system_api::cicerone_service::{self, *};
use proto::system_api::seneschal_service::*;
use proto::system_api::service::*;
use proto::system_api::vm_plugin_dispatcher;
use proto::system_api::vm_plugin_dispatcher::VmErrorCode;

const IMAGE_TYPE_QCOW2: &str = "qcow2";
const REMOVABLE_MEDIA_ROOT: &str = "/media/removable";
const CRYPTOHOME_USER: &str = "/home/user";
const CRYPTOHOME_ROOT: &str = "/home";
const DOWNLOADS_DIR: &str = "Downloads";
const MNT_SHARED_ROOT: &str = "/mnt/shared";

/// Round to disk block size.
const DISK_SIZE_MASK: u64 = !511;
const DEFAULT_TIMEOUT_MS: i32 = 80 * 1000;
const EXPORT_DISK_TIMEOUT_MS: i32 = 15 * 60 * 1000;
const COMPONENT_UPDATER_TIMEOUT_MS: i32 = 120 * 1000;

// debugd dbus-constants.h
const DEBUGD_INTERFACE: &str = "org.chromium.debugd";
const DEBUGD_SERVICE_PATH: &str = "/org/chromium/debugd";
const DEBUGD_SERVICE_NAME: &str = "org.chromium.debugd";
const START_VM_CONCIERGE: &str = "StartVmConcierge";
const START_VM_PLUGIN_DISPATCHER: &str = "StartVmPluginDispatcher";

// Chrome dbus service_constants.h
const CHROME_FEATURES_INTERFACE: &str = "org.chromium.ChromeFeaturesServiceInterface";
const CHROME_FEATURES_SERVICE_PATH: &str = "/org/chromium/ChromeFeaturesService";
const CHROME_FEATURES_SERVICE_NAME: &str = "org.chromium.ChromeFeaturesService";
const IS_CROSTINI_ENABLED: &str = "IsCrostiniEnabled";
const IS_PLUGIN_VM_ENABLED: &str = "IsPluginVmEnabled";

// concierge dbus-constants.h
const VM_CONCIERGE_INTERFACE: &str = "org.chromium.VmConcierge";
const VM_CONCIERGE_SERVICE_PATH: &str = "/org/chromium/VmConcierge";
const VM_CONCIERGE_SERVICE_NAME: &str = "org.chromium.VmConcierge";
const START_VM_METHOD: &str = "StartVm";
const STOP_VM_METHOD: &str = "StopVm";
const STOP_ALL_VMS_METHOD: &str = "StopAllVms";
const GET_VM_INFO_METHOD: &str = "GetVmInfo";
const CREATE_DISK_IMAGE_METHOD: &str = "CreateDiskImage";
const DESTROY_DISK_IMAGE_METHOD: &str = "DestroyDiskImage";
const EXPORT_DISK_IMAGE_METHOD: &str = "ExportDiskImage";
const IMPORT_DISK_IMAGE_METHOD: &str = "ImportDiskImage";
const DISK_IMAGE_STATUS_METHOD: &str = "DiskImageStatus";
const LIST_VM_DISKS_METHOD: &str = "ListVmDisks";
const START_CONTAINER_METHOD: &str = "StartContainer";
const GET_CONTAINER_SSH_KEYS_METHOD: &str = "GetContainerSshKeys";
const SYNC_VM_TIMES_METHOD: &str = "SyncVmTimes";
const ATTACH_USB_DEVICE_METHOD: &str = "AttachUsbDevice";
const DETACH_USB_DEVICE_METHOD: &str = "DetachUsbDevice";
const LIST_USB_DEVICE_METHOD: &str = "ListUsbDevices";
const CONTAINER_STARTUP_FAILED_SIGNAL: &str = "ContainerStartupFailed";
const DISK_IMAGE_PROGRESS_SIGNAL: &str = "DiskImageProgress";

// vm_plugin_dispatcher dbus-constants.h
const VM_PLUGIN_DISPATCHER_INTERFACE: &str = "org.chromium.VmPluginDispatcher";
const VM_PLUGIN_DISPATCHER_SERVICE_PATH: &str = "/org/chromium/VmPluginDispatcher";
const VM_PLUGIN_DISPATCHER_SERVICE_NAME: &str = "org.chromium.VmPluginDispatcher";
const START_PLUGIN_VM_METHOD: &str = "StartVm";
const SHOW_PLUGIN_VM_METHOD: &str = "ShowVm";

// cicerone dbus-constants.h
const VM_CICERONE_INTERFACE: &str = "org.chromium.VmCicerone";
const VM_CICERONE_SERVICE_PATH: &str = "/org/chromium/VmCicerone";
const VM_CICERONE_SERVICE_NAME: &str = "org.chromium.VmCicerone";
const NOTIFY_VM_STARTED_METHOD: &str = "NotifyVmStarted";
const NOTIFY_VM_STOPPED_METHOD: &str = "NotifyVmStopped";
const GET_CONTAINER_TOKEN_METHOD: &str = "GetContainerToken";
const LAUNCH_CONTAINER_APPLICATION_METHOD: &str = "LaunchContainerApplication";
const GET_CONTAINER_APP_ICON_METHOD: &str = "GetContainerAppIcon";
const LAUNCH_VSHD_METHOD: &str = "LaunchVshd";
const GET_LINUX_PACKAGE_INFO_METHOD: &str = "GetLinuxPackageInfo";
const INSTALL_LINUX_PACKAGE_METHOD: &str = "InstallLinuxPackage";
const UNINSTALL_PACKAGE_OWNING_FILE_METHOD: &str = "UninstallPackageOwningFile";
const CREATE_LXD_CONTAINER_METHOD: &str = "CreateLxdContainer";
const START_LXD_CONTAINER_METHOD: &str = "StartLxdContainer";
const GET_LXD_CONTAINER_USERNAME_METHOD: &str = "GetLxdContainerUsername";
const SET_UP_LXD_CONTAINER_USER_METHOD: &str = "SetUpLxdContainerUser";
const APP_SEARCH_METHOD: &str = "AppSearch";
const GET_DEBUG_INFORMATION: &str = "GetDebugInformation";
const CONTAINER_STARTED_SIGNAL: &str = "ContainerStarted";
const CONTAINER_SHUTDOWN_SIGNAL: &str = "ContainerShutdown";
const INSTALL_LINUX_PACKAGE_PROGRESS_SIGNAL: &str = "InstallLinuxPackageProgress";
const UNINSTALL_PACKAGE_PROGRESS_SIGNAL: &str = "UninstallPackageProgress";
const LXD_CONTAINER_CREATED_SIGNAL: &str = "LxdContainerCreated";
const LXD_CONTAINER_DOWNLOADING_SIGNAL: &str = "LxdContainerDownloading";
const LXD_CONTAINER_STARTING_SIGNAL: &str = "LxdContainerStarting";
const TREMPLIN_STARTED_SIGNAL: &str = "TremplinStarted";

// seneschal dbus-constants.h
const SENESCHAL_INTERFACE: &str = "org.chromium.Seneschal";
const SENESCHAL_SERVICE_PATH: &str = "/org/chromium/Seneschal";
const SENESCHAL_SERVICE_NAME: &str = "org.chromium.Seneschal";
const START_SERVER_METHOD: &str = "StartServer";
const STOP_SERVER_METHOD: &str = "StopServer";
const SHARE_PATH_METHOD: &str = "SharePath";
const UNSHARE_PATH_METHOD: &str = "UnsharePath";

// permission_broker dbus-constants.h
const PERMISSION_BROKER_INTERFACE: &str = "org.chromium.PermissionBroker";
const PERMISSION_BROKER_SERVICE_PATH: &str = "/org/chromium/PermissionBroker";
const PERMISSION_BROKER_SERVICE_NAME: &str = "org.chromium.PermissionBroker";
const CHECK_PATH_ACCESS: &str = "CheckPathAccess";
const OPEN_PATH: &str = "OpenPath";
const REQUEST_TCP_PORT_ACCESS: &str = "RequestTcpPortAccess";
const REQUEST_UDP_PORT_ACCESS: &str = "RequestUdpPortAccess";
const RELEASE_TCP_PORT: &str = "ReleaseTcpPort";
const RELEASE_UDP_PORT: &str = "ReleaseUdpPort";
const REQUEST_VPN_SETUP: &str = "RequestVpnSetup";
const REMOVE_VPN_SETUP: &str = "RemoveVpnSetup";
const POWER_CYCLE_USB_PORTS: &str = "PowerCycleUsbPorts";

enum ChromeOSError {
    BadChromeFeatureStatus,
    BadConciergeStatus,
    BadDiskImageStatus(DiskImageStatus, String),
    BadPluginVmStatus(VmErrorCode),
    BadVmStatus(VmStatus, String),
    BadVmPluginDispatcherStatus,
    CrostiniVmDisabled,
    ExportPathExists,
    ImportPathDoesNotExist,
    FailedAttachUsb(String),
    FailedComponentUpdater(String),
    FailedCreateContainer(CreateLxdContainerResponse_Status, String),
    FailedCreateContainerSignal(LxdContainerCreatedSignal_Status, String),
    FailedDetachUsb(String),
    FailedGetOpenPath(PathBuf),
    FailedGetVmInfo,
    FailedListDiskImages(String),
    FailedListUsb,
    FailedMetricsSend { exit_code: Option<i32> },
    FailedOpenPath(dbus::Error),
    FailedSetupContainerUser(SetUpLxdContainerUserResponse_Status, String),
    FailedSharePath(String),
    FailedStartContainerSignal(LxdContainerStartingSignal_Status, String),
    FailedStartContainerStatus(StartLxdContainerResponse_Status, String),
    FailedStopVm { vm_name: String, reason: String },
    InvalidExportPath,
    InvalidImportPath,
    InvalidSourcePath,
    NoVmTechnologyEnabled,
    NotAvailableForPluginVm,
    PluginVmDisabled,
    RetrieveActiveSessions,
    SourcePathDoesNotExist,
    TpmOnStable,
}

use self::ChromeOSError::*;

impl fmt::Display for ChromeOSError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BadChromeFeatureStatus => write!(f, "invalid response to chrome feature request"),
            BadConciergeStatus => write!(f, "failed to start concierge"),
            BadDiskImageStatus(s, reason) => {
                write!(f, "bad disk image status: `{:?}`: {}", s, reason)
            }
            BadPluginVmStatus(s) => write!(f, "bad plugin VM status: `{:?}`", s),
            BadVmStatus(s, reason) => write!(f, "bad VM status: `{:?}`: {}", s, reason),
            BadVmPluginDispatcherStatus => write!(f, "failed to start Plugin VM dispatcher"),
            CrostiniVmDisabled => write!(f, "Crostini VMs are currently disabled"),
            ExportPathExists => write!(f, "disk export path already exists"),
            ImportPathDoesNotExist => write!(f, "disk import path does not exist"),
            FailedAttachUsb(reason) => write!(f, "failed to attach usb device to vm: {}", reason),
            FailedDetachUsb(reason) => write!(f, "failed to detach usb device from vm: {}", reason),
            FailedComponentUpdater(name) => {
                write!(f, "component updater could not load component `{}`", name)
            }
            FailedCreateContainer(s, reason) => {
                write!(f, "failed to create container: `{:?}`: {}", s, reason)
            }
            FailedCreateContainerSignal(s, reason) => {
                write!(f, "failed to create container: `{:?}`: {}", s, reason)
            }
            FailedStartContainerSignal(s, reason) => {
                write!(f, "failed to start container: `{:?}`: {}", s, reason)
            }
            FailedGetOpenPath(path) => write!(f, "failed to request OpenPath {}", path.display()),
            FailedGetVmInfo => write!(f, "failed to get vm info"),
            FailedSetupContainerUser(s, reason) => {
                write!(f, "failed to setup container user: `{:?}`: {}", s, reason)
            }
            FailedSharePath(reason) => write!(f, "failed to share path with vm: {}", reason),
            FailedStartContainerStatus(s, reason) => {
                write!(f, "failed to start container: `{:?}`: {}", s, reason)
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
            FailedOpenPath(e) => write!(
                f,
                "failed permission_broker OpenPath: {}",
                e.message().unwrap_or("")
            ),
            FailedStopVm { vm_name, reason } => {
                write!(f, "failed to stop vm `{}`: {}", vm_name, reason)
            }
            InvalidExportPath => write!(f, "disk export path is invalid"),
            InvalidImportPath => write!(f, "disk import path is invalid"),
            InvalidSourcePath => write!(f, "source media path is invalid"),
            NoVmTechnologyEnabled => write!(f, "neither Crostini nor Plugin VMs are enabled"),
            NotAvailableForPluginVm => write!(f, "this command is not available for Plugin VM"),
            PluginVmDisabled => write!(f, "Plugin VMs are currently disabled"),
            RetrieveActiveSessions => write!(f, "failed to retrieve active sessions"),
            SourcePathDoesNotExist => write!(f, "source media path does not exist"),
            TpmOnStable => write!(f, "TPM device is not available on stable channel"),
        }
    }
}

impl fmt::Debug for ChromeOSError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        <Self as fmt::Display>::fmt(self, f)
    }
}

impl Error for ChromeOSError {}

fn dbus_message_to_proto<T: ProtoMessage>(message: &Message) -> Result<T, Box<Error>> {
    let raw_buffer: Vec<u8> = message.read1()?;
    let mut proto = T::new();
    proto.merge_from_bytes(&raw_buffer)?;
    Ok(proto)
}

/// Uses the standard ChromeOS interfaces to implement the backends methods with the least possible
/// privilege. Uses a combination of D-Bus, protobufs, and shell protocols.
pub struct ChromeOS {
    connection: Connection,
    crostini_enabled: Option<bool>,
    plugin_vm_enabled: Option<bool>,
}

impl ChromeOS {
    /// Initiates a D-Bus connection and returns an initialized backend.
    pub fn new() -> Result<ChromeOS, Box<Error>> {
        let connection = Connection::get_private(BusType::System)?;
        Ok(ChromeOS {
            connection: connection,
            crostini_enabled: None,
            plugin_vm_enabled: None,
        })
    }
}

impl ChromeOS {
    /// Helper for doing protobuf over dbus requests and responses.
    fn sync_protobus<I: ProtoMessage, O: ProtoMessage>(
        &mut self,
        message: Message,
        request: &I,
    ) -> Result<O, Box<Error>> {
        self.sync_protobus_timeout(message, request, &[], DEFAULT_TIMEOUT_MS)
    }

    /// Helper for doing protobuf over dbus requests and responses.
    fn sync_protobus_timeout<I: ProtoMessage, O: ProtoMessage>(
        &mut self,
        message: Message,
        request: &I,
        fds: &[OwnedFd],
        timeout_millis: i32,
    ) -> Result<O, Box<Error>> {
        let method = message.append1(request.write_to_bytes()?).append_ref(fds);
        let message = self
            .connection
            .send_with_reply_and_block(method, timeout_millis)?;
        dbus_message_to_proto(&message)
    }

    fn protobus_wait_for_signal_timeout<O: ProtoMessage>(
        &mut self,
        interface: &str,
        signal: &str,
        timeout_millis: i32,
    ) -> Result<O, Box<Error>> {
        let rule = format!("interface='{}',member='{}'", interface, signal);
        self.connection.add_match(&rule)?;
        let mut result: Result<O, Box<Error>> = Err("timeout while waiting for signal".into());
        for item in self.connection.iter(timeout_millis) {
            match item {
                ConnectionItem::Signal(message) => {
                    if let (_, _, Some(msg_interface), Some(msg_signal)) = message.headers() {
                        if msg_interface == interface && signal == msg_signal {
                            result = dbus_message_to_proto(&message);
                            break;
                        }
                    }
                }
                ConnectionItem::Nothing => break,
                _ => {}
            };
        }
        self.connection.remove_match(&rule)?;
        result
    }

    /// Request that component updater load a named component.
    fn component_updater_load_component(&mut self, name: &str) -> Result<(), Box<Error>> {
        let method = Message::new_method_call(
            "org.chromium.ComponentUpdaterService",
            "/org/chromium/ComponentUpdaterService",
            "org.chromium.ComponentUpdaterService",
            "LoadComponent",
        )?
        .append1(name);
        let message = self
            .connection
            .send_with_reply_and_block(method, COMPONENT_UPDATER_TIMEOUT_MS)?;
        match message.get1() {
            Some("") | None => Err(FailedComponentUpdater(name.to_owned()).into()),
            _ => Ok(()),
        }
    }

    fn is_chrome_feature_enabled(
        &mut self,
        user_id_hash: &str,
        feature_name: &str,
    ) -> Result<bool, Box<Error>> {
        let method = Message::new_method_call(
            CHROME_FEATURES_SERVICE_NAME,
            CHROME_FEATURES_SERVICE_PATH,
            CHROME_FEATURES_INTERFACE,
            feature_name,
        )?
        .append1(user_id_hash);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;
        match message.get1() {
            Some(true) => Ok(true),
            Some(false) => Ok(false),
            _ => Err(BadChromeFeatureStatus.into()),
        }
    }

    fn is_crostini_enabled(&mut self, user_id_hash: &str) -> Result<bool, Box<Error>> {
        let enabled = match self.crostini_enabled {
            Some(value) => value,
            None => {
                let value = self.is_chrome_feature_enabled(user_id_hash, IS_CROSTINI_ENABLED)?;
                self.crostini_enabled = Some(value);
                value
            }
        };
        Ok(enabled)
    }

    fn is_plugin_vm_enabled(&mut self, user_id_hash: &str) -> Result<bool, Box<Error>> {
        let enabled = match self.plugin_vm_enabled {
            Some(value) => value,
            None => {
                let value = self.is_chrome_feature_enabled(user_id_hash, IS_PLUGIN_VM_ENABLED)?;
                self.plugin_vm_enabled = Some(value);
                value
            }
        };
        Ok(enabled)
    }

    /// Request debugd to start vm_concierge.
    fn start_concierge(&mut self) -> Result<(), Box<Error>> {
        // Mount the termina component, waiting up to 2 minutes to download it. If this fails we
        // won't be able to start the concierge service below.
        self.component_updater_load_component("cros-termina")?;

        let method = Message::new_method_call(
            DEBUGD_SERVICE_NAME,
            DEBUGD_SERVICE_PATH,
            DEBUGD_INTERFACE,
            START_VM_CONCIERGE,
        )?;

        let message = self.connection.send_with_reply_and_block(method, 30000)?;
        match message.get1() {
            Some(true) => Ok(()),
            _ => Err(BadConciergeStatus.into()),
        }
    }

    /// Request debugd to start vmplugin_dispatcher.
    fn start_vm_plugin_dispather(&mut self) -> Result<(), Box<Error>> {
        // TODO(dtor): download the component before trying to start it.

        let method = Message::new_method_call(
            DEBUGD_SERVICE_NAME,
            DEBUGD_SERVICE_PATH,
            DEBUGD_INTERFACE,
            START_VM_PLUGIN_DISPATCHER,
        )?;

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;
        match message.get1() {
            Some(true) => Ok(()),
            _ => Err(BadVmPluginDispatcherStatus.into()),
        }
    }

    /// Starts all necessary VM services (concierge and optionally the Plugin VM dispatcher).
    fn start_vm_infrastructure(&mut self, user_id_hash: &str) -> Result<(), Box<Error>> {
        if self.is_plugin_vm_enabled(user_id_hash)? {
            // Starting the dispatcher will also start concierge.
            self.start_vm_plugin_dispather()
        } else if self.is_crostini_enabled(user_id_hash)? {
            self.start_concierge()
        } else {
            Err(NoVmTechnologyEnabled.into())
        }
    }

    /// Request that concierge create a disk image.
    fn create_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<String, Box<Error>> {
        let mut request = CreateDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.image_type = DiskImageType::DISK_IMAGE_AUTO;
        request.storage_location = StorageLocation::STORAGE_CRYPTOHOME_ROOT;

        let response: CreateDiskImageResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                CREATE_DISK_IMAGE_METHOD,
            )?,
            &request,
        )?;

        match response.status {
            DiskImageStatus::DISK_STATUS_EXISTS | DiskImageStatus::DISK_STATUS_CREATED => {
                Ok(response.disk_path)
            }
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge create a new VM image.
    fn create_vm_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        source_name: Option<&str>,
        removable_media: Option<&str>,
        params: &[&str],
    ) -> Result<Option<String>, Box<Error>> {
        let mut request = CreateDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.image_type = DiskImageType::DISK_IMAGE_AUTO;
        request.storage_location = if plugin_vm {
            if !self.is_plugin_vm_enabled(user_id_hash)? {
                return Err(PluginVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM
        } else {
            if !self.is_crostini_enabled(user_id_hash)? {
                return Err(CrostiniVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_ROOT
        };

        let source_fd = match source_name {
            Some(source) => {
                let source_path = match removable_media {
                    Some(media_path) => Path::new(REMOVABLE_MEDIA_ROOT)
                        .join(media_path)
                        .join(source),
                    None => Path::new(CRYPTOHOME_USER)
                        .join(user_id_hash)
                        .join(DOWNLOADS_DIR)
                        .join(source),
                };

                if source_path.components().any(|c| c == Component::ParentDir) {
                    return Err(InvalidSourcePath.into());
                }

                if !source_path.exists() {
                    return Err(SourcePathDoesNotExist.into());
                }

                let source_file = OpenOptions::new().read(true).open(source_path)?;
                request.source_size = source_file.metadata()?.len();
                Some(OwnedFd::new(source_file.into_raw_fd()))
            }
            None => None,
        };

        for param in params {
            request.mut_params().push(param.to_string());
        }

        // We can't use sync_protobus because we need to append the file descriptor out of band from
        // the protobuf message.
        let mut method = Message::new_method_call(
            VM_CONCIERGE_SERVICE_NAME,
            VM_CONCIERGE_SERVICE_PATH,
            VM_CONCIERGE_INTERFACE,
            CREATE_DISK_IMAGE_METHOD,
        )?
        .append1(request.write_to_bytes()?);
        if let Some(fd) = source_fd {
            method = method.append1(fd);
        }

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;

        let response: CreateDiskImageResponse = dbus_message_to_proto(&message)?;
        match response.status {
            DiskImageStatus::DISK_STATUS_CREATED => Ok(None),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok(Some(response.command_uuid)),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge create a disk image.
    fn destroy_disk_image(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>> {
        let mut request = DestroyDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();

        let response: DestroyDiskImageResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                DESTROY_DISK_IMAGE_METHOD,
            )?,
            &request,
        )?;

        match response.status {
            DiskImageStatus::DISK_STATUS_DESTROYED
            | DiskImageStatus::DISK_STATUS_DOES_NOT_EXIST => Ok(()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge export a VM's disk image.
    fn export_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        export_name: &str,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<Error>> {
        let export_path = match removable_media {
            Some(media_path) => Path::new(REMOVABLE_MEDIA_ROOT)
                .join(media_path)
                .join(export_name),
            None => Path::new(CRYPTOHOME_USER)
                .join(user_id_hash)
                .join(DOWNLOADS_DIR)
                .join(export_name),
        };

        if export_path.components().any(|c| c == Component::ParentDir) {
            return Err(InvalidExportPath.into());
        }

        if export_path.exists() {
            return Err(ExportPathExists.into());
        }

        // Exporting the disk should always create a new file, and only be accessible to the user
        // that creates it. The old version of this used `O_NOFOLLOW` in its open flags, but this
        // has no effect as `O_NOFOLLOW` only preempts symlinks for the final part of the path,
        // which is guaranteed to not exist by `create_new(true)`.
        let export_file = OpenOptions::new()
            .write(true)
            .read(true)
            .create_new(true)
            .mode(0o600)
            .open(export_path)?;
        let export_fd = OwnedFd::new(export_file.into_raw_fd());

        let mut request = ExportDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();

        // We can't use sync_protobus because we need to append the file descriptor out of band from
        // the protobuf message.
        let method = Message::new_method_call(
            VM_CONCIERGE_SERVICE_NAME,
            VM_CONCIERGE_SERVICE_PATH,
            VM_CONCIERGE_INTERFACE,
            EXPORT_DISK_IMAGE_METHOD,
        )?
        .append1(request.write_to_bytes()?)
        .append1(export_fd);

        let message = self
            .connection
            .send_with_reply_and_block(method, EXPORT_DISK_TIMEOUT_MS)?;

        let response: ExportDiskImageResponse = dbus_message_to_proto(&message)?;
        match response.status {
            DiskImageStatus::DISK_STATUS_CREATED => Ok(None),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok(Some(response.command_uuid)),
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
    ) -> Result<Option<String>, Box<Error>> {
        let import_path = match removable_media {
            Some(media_path) => Path::new(REMOVABLE_MEDIA_ROOT)
                .join(media_path)
                .join(import_name),
            None => Path::new(CRYPTOHOME_USER)
                .join(user_id_hash)
                .join(DOWNLOADS_DIR)
                .join(import_name),
        };

        if import_path.components().any(|c| c == Component::ParentDir) {
            return Err(InvalidImportPath.into());
        }

        if !import_path.exists() {
            return Err(ImportPathDoesNotExist.into());
        }

        let import_file = OpenOptions::new().read(true).open(import_path)?;
        let file_size = import_file.metadata()?.len();
        let import_fd = OwnedFd::new(import_file.into_raw_fd());

        let mut request = ImportDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.storage_location = if plugin_vm {
            if !self.is_plugin_vm_enabled(user_id_hash)? {
                return Err(PluginVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM
        } else {
            if !self.is_crostini_enabled(user_id_hash)? {
                return Err(CrostiniVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_ROOT
        };
        request.source_size = file_size;

        // We can't use sync_protobus because we need to append the file descriptor out of band from
        // the protobuf message.
        let method = Message::new_method_call(
            VM_CONCIERGE_SERVICE_NAME,
            VM_CONCIERGE_SERVICE_PATH,
            VM_CONCIERGE_INTERFACE,
            IMPORT_DISK_IMAGE_METHOD,
        )?
        .append1(request.write_to_bytes()?)
        .append1(import_fd);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;

        let response: ImportDiskImageResponse = dbus_message_to_proto(&message)?;
        match response.status {
            DiskImageStatus::DISK_STATUS_CREATED => Ok(None),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok(Some(response.command_uuid)),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    fn parse_disk_op_status(
        &mut self,
        response: DiskImageStatusResponse,
    ) -> Result<(bool, u32), Box<Error>> {
        match response.status {
            DiskImageStatus::DISK_STATUS_CREATED => Ok((true, response.progress)),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok((false, response.progress)),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request concierge to provide status of a disk operation (import or export) with given UUID.
    fn check_disk_operation(&mut self, uuid: &str) -> Result<(bool, u32), Box<Error>> {
        let mut request = DiskImageStatusRequest::new();
        request.command_uuid = uuid.to_owned();

        let response: DiskImageStatusResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                DISK_IMAGE_STATUS_METHOD,
            )?,
            &request,
        )?;

        self.parse_disk_op_status(response)
    }

    /// Wait for updated status of a disk operation (import or export) with given UUID.
    fn wait_disk_operation(&mut self, uuid: &str) -> Result<(bool, u32), Box<Error>> {
        loop {
            let response: DiskImageStatusResponse = self.protobus_wait_for_signal_timeout(
                VM_CONCIERGE_INTERFACE,
                DISK_IMAGE_PROGRESS_SIGNAL,
                DEFAULT_TIMEOUT_MS,
            )?;

            if response.command_uuid == uuid {
                return self.parse_disk_op_status(response);
            }
        }
    }

    /// Request a list of disk images from concierge.
    fn list_disk_images(
        &mut self,
        user_id_hash: &str,
        target_location: Option<StorageLocation>,
        target_name: Option<&str>,
    ) -> Result<(Vec<VmDiskInfo>, u64), Box<Error>> {
        let mut request = ListVmDisksRequest::new();
        request.cryptohome_id = user_id_hash.to_owned();
        match target_location {
            Some(location) => request.storage_location = location,
            None => request.all_locations = true,
        };
        if let Some(vm_name) = target_name {
            request.vm_name = vm_name.to_string();
        }

        let response: ListVmDisksResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                LIST_VM_DISKS_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok((response.images.into(), response.total_size))
        } else {
            Err(FailedListDiskImages(response.failure_reason).into())
        }
    }

    /// Checks if VM with given name/disk is Plugin VM.
    fn is_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<bool, Box<Error>> {
        let (images, _) = self.list_disk_images(
            user_id_hash,
            Some(StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM),
            Some(vm_name),
        )?;
        Ok(images.len() != 0)
    }

    /// Request that concierge start a vm with the given disk image.
    fn start_vm_with_disk(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        features: VmFeatures,
        disk_image_path: String,
    ) -> Result<(), Box<Error>> {
        let mut request = StartVmRequest::new();
        request.start_termina = true;
        request.owner_id = user_id_hash.to_owned();
        request.enable_gpu = features.gpu;
        request.software_tpm = features.software_tpm;
        request.name = vm_name.to_owned();
        {
            let disk_image = request.mut_disks().push_default();
            disk_image.path = disk_image_path;
            disk_image.writable = true;
            disk_image.do_mount = false;
        }

        let response: StartVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                START_VM_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            return Ok(());
        }

        match response.status {
            VmStatus::VM_STATUS_RUNNING | VmStatus::VM_STATUS_STARTING => Ok(()),
            _ => Err(BadVmStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that dispatcher start given Plugin VM.
    fn start_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>> {
        let mut request = vm_plugin_dispatcher::StartVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name_uuid = vm_name.to_owned();

        let response: vm_plugin_dispatcher::StartVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                START_PLUGIN_VM_METHOD,
            )?,
            &request,
        )?;

        match response.error {
            VmErrorCode::VM_SUCCESS => Ok(()),
            _ => Err(BadPluginVmStatus(response.error).into()),
        }
    }

    /// Request that dispatcher starts application responsible for rendering Plugin VM window.
    fn show_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>> {
        let mut request = vm_plugin_dispatcher::ShowVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name_uuid = vm_name.to_owned();

        let response: vm_plugin_dispatcher::ShowVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                SHOW_PLUGIN_VM_METHOD,
            )?,
            &request,
        )?;

        match response.error {
            VmErrorCode::VM_SUCCESS => Ok(()),
            _ => Err(BadPluginVmStatus(response.error).into()),
        }
    }

    /// Request that `concierge` stop a vm with the given disk image.
    fn stop_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>> {
        let mut request = StopVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.name = vm_name.to_owned();

        let response: StopVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                STOP_VM_METHOD,
            )?,
            &request,
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
    fn get_vm_info(&mut self, vm_name: &str, user_id_hash: &str) -> Result<VmInfo, Box<Error>> {
        let mut request = GetVmInfoRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.name = vm_name.to_owned();

        let response: GetVmInfoResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                GET_VM_INFO_METHOD,
            )?,
            &request,
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
    ) -> Result<String, Box<Error>> {
        let mut request = SharePathRequest::new();
        request.handle = seneschal_handle;
        request.shared_path.set_default().path = path.to_owned();
        request.storage_location = SharePathRequest_StorageLocation::MY_FILES;
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
    ) -> Result<(), Box<Error>> {
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
        image_server: &str,
        image_alias: &str,
    ) -> Result<(), Box<Error>> {
        let mut request = CreateLxdContainerRequest::new();
        request.vm_name = vm_name.to_owned();
        request.container_name = container_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.image_server = image_server.to_owned();
        request.image_alias = image_alias.to_owned();

        let response: CreateLxdContainerResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CICERONE_SERVICE_NAME,
                VM_CICERONE_SERVICE_PATH,
                VM_CICERONE_INTERFACE,
                CREATE_LXD_CONTAINER_METHOD,
            )?,
            &request,
        )?;

        use self::CreateLxdContainerResponse_Status::*;
        use self::LxdContainerCreatedSignal_Status::*;
        match response.status {
            CREATING => {
                let signal: LxdContainerCreatedSignal = self.protobus_wait_for_signal_timeout(
                    VM_CICERONE_INTERFACE,
                    LXD_CONTAINER_CREATED_SIGNAL,
                    DEFAULT_TIMEOUT_MS,
                )?;
                match signal.status {
                    CREATED => Ok(()),
                    _ => Err(
                        FailedCreateContainerSignal(signal.status, signal.failure_reason).into(),
                    ),
                }
            }
            EXISTS => Ok(()),
            _ => Err(FailedCreateContainer(response.status, response.failure_reason).into()),
        }
    }

    fn start_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
    ) -> Result<(), Box<Error>> {
        let mut request = StartLxdContainerRequest::new();
        request.vm_name = vm_name.to_owned();
        request.container_name = container_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.async = true;

        let response: StartLxdContainerResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CICERONE_SERVICE_NAME,
                VM_CICERONE_SERVICE_PATH,
                VM_CICERONE_INTERFACE,
                START_LXD_CONTAINER_METHOD,
            )?,
            &request,
        )?;

        use self::StartLxdContainerResponse_Status::*;
        match response.status {
            STARTING => {
                let signal: cicerone_service::LxdContainerStartingSignal = self
                    .protobus_wait_for_signal_timeout(
                        VM_CICERONE_INTERFACE,
                        LXD_CONTAINER_STARTING_SIGNAL,
                        DEFAULT_TIMEOUT_MS,
                    )?;
                match signal.status {
                    LxdContainerStartingSignal_Status::STARTED => Ok(()),
                    _ => {
                        Err(FailedStartContainerSignal(signal.status, signal.failure_reason).into())
                    }
                }
            }
            RUNNING => Ok(()),
            _ => Err(FailedStartContainerStatus(response.status, response.failure_reason).into()),
        }
    }

    fn setup_container_user(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        username: &str,
    ) -> Result<(), Box<Error>> {
        let mut request = SetUpLxdContainerUserRequest::new();
        request.vm_name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.container_name = container_name.to_owned();
        request.container_username = username.to_owned();

        let response: SetUpLxdContainerUserResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CICERONE_SERVICE_NAME,
                VM_CICERONE_SERVICE_PATH,
                VM_CICERONE_INTERFACE,
                SET_UP_LXD_CONTAINER_USER_METHOD,
            )?,
            &request,
        )?;

        use self::SetUpLxdContainerUserResponse_Status::*;
        match response.status {
            SUCCESS | EXISTS => {
                // TODO: listen for signal before calling the D-Bus method.
                let _signal: cicerone_service::ContainerStartedSignal = self
                    .protobus_wait_for_signal_timeout(
                        VM_CICERONE_INTERFACE,
                        CONTAINER_STARTED_SIGNAL,
                        DEFAULT_TIMEOUT_MS,
                    )?;
                Ok(())
            }
            _ => Err(FailedSetupContainerUser(response.status, response.failure_reason).into()),
        }
    }

    fn attach_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        bus: u8,
        device: u8,
        usb_fd: OwnedFd,
    ) -> Result<u8, Box<Error>> {
        let mut request = AttachUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.bus_number = bus as u32;
        request.port_number = device as u32;

        let response: AttachUsbDeviceResponse = self.sync_protobus_timeout(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                ATTACH_USB_DEVICE_METHOD,
            )?,
            &request,
            &[usb_fd],
            DEFAULT_TIMEOUT_MS,
        )?;

        if response.success {
            Ok(response.guest_port as u8)
        } else {
            Err(FailedAttachUsb(response.reason).into())
        }
    }

    fn detach_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        port: u8,
    ) -> Result<(), Box<Error>> {
        let mut request = DetachUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.guest_port = port as u32;

        let response: DetachUsbDeviceResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                DETACH_USB_DEVICE_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedDetachUsb(response.reason).into())
        }
    }

    fn list_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<Vec<UsbDeviceMessage>, Box<Error>> {
        let mut request = ListUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();

        let response: ListUsbDeviceResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                LIST_USB_DEVICE_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(response.usb_devices.into())
        } else {
            Err(FailedListUsb.into())
        }
    }

    fn permission_broker_open_path(&mut self, path: &Path) -> Result<OwnedFd, Box<Error>> {
        let path_str = path
            .to_str()
            .ok_or_else(|| FailedGetOpenPath(path.into()))?;
        let method = Message::new_method_call(
            PERMISSION_BROKER_SERVICE_NAME,
            PERMISSION_BROKER_SERVICE_PATH,
            PERMISSION_BROKER_INTERFACE,
            OPEN_PATH,
        )?
        .append1(path_str);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)
            .map_err(FailedOpenPath)?;

        message
            .get1()
            .ok_or_else(|| FailedGetOpenPath(path.into()).into())
    }
}

impl Backend for ChromeOS {
    fn name(&self) -> &'static str {
        "ChromeOS"
    }

    fn metrics_send_sample(&mut self, name: &str) -> Result<(), Box<Error>> {
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

    fn sessions_list(&mut self) -> Result<Vec<(String, String)>, Box<Error>> {
        let method = Message::new_method_call(
            "org.chromium.SessionManager",
            "/org/chromium/SessionManager",
            "org.chromium.SessionManagerInterface",
            "RetrieveActiveSessions",
        )?;
        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;
        match message.get1::<HashMap<String, String>>() {
            Some(sessions) => Ok(sessions.into_iter().collect()),
            _ => Err(RetrieveActiveSessions.into()),
        }
    }

    fn vm_create(
        &mut self,
        name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        source_name: Option<&str>,
        removable_media: Option<&str>,
        params: &[&str],
    ) -> Result<Option<String>, Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.create_vm_image(
            name,
            user_id_hash,
            plugin_vm,
            source_name,
            removable_media,
            params,
        )
    }

    fn vm_start(
        &mut self,
        name: &str,
        user_id_hash: &str,
        features: VmFeatures,
    ) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(name, user_id_hash)? {
            if !self.is_plugin_vm_enabled(user_id_hash)? {
                return Err(PluginVmDisabled.into());
            }
            self.start_plugin_vm(name, user_id_hash)
        } else {
            if !self.is_crostini_enabled(user_id_hash)? {
                return Err(CrostiniVmDisabled.into());
            }

            let is_stable_channel = is_stable_channel();
            if features.software_tpm && is_stable_channel {
                return Err(TpmOnStable.into());
            }

            let disk_image_path = self.create_disk_image(name, user_id_hash)?;
            self.start_vm_with_disk(name, user_id_hash, features, disk_image_path)
        }
    }

    fn vm_stop(&mut self, name: &str, user_id_hash: &str) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.stop_vm(name, user_id_hash)
    }

    fn vm_export(
        &mut self,
        name: &str,
        user_id_hash: &str,
        file_name: &str,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.export_disk_image(name, user_id_hash, file_name, removable_media)
    }

    fn vm_import(
        &mut self,
        name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        file_name: &str,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.import_disk_image(name, user_id_hash, plugin_vm, file_name, removable_media)
    }

    fn vm_share_path(
        &mut self,
        name: &str,
        user_id_hash: &str,
        path: &str,
    ) -> Result<String, Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let vm_info = self.get_vm_info(name, user_id_hash)?;
        // The VmInfo uses a u64 as the handle, but SharePathRequest uses a u32 for the handle.
        if vm_info.seneschal_server_handle > u64::from(u32::max_value()) {
            return Err(FailedGetVmInfo.into());
        }
        let seneschal_handle = vm_info.seneschal_server_handle as u32;
        let vm_path = self.share_path_with_vm(seneschal_handle, user_id_hash, path)?;
        Ok(format!("{}/{}", MNT_SHARED_ROOT, vm_path))
    }

    fn vm_unshare_path(
        &mut self,
        name: &str,
        user_id_hash: &str,
        path: &str,
    ) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let vm_info = self.get_vm_info(name, user_id_hash)?;
        // The VmInfo uses a u64 as the handle, but SharePathRequest uses a u32 for the handle.
        if vm_info.seneschal_server_handle > u64::from(u32::max_value()) {
            return Err(FailedGetVmInfo.into());
        }
        self.unshare_path_with_vm(vm_info.seneschal_server_handle as u32, path)
    }

    fn vsh_exec(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            self.show_plugin_vm(vm_name, user_id_hash)
        } else {
            Command::new("vsh")
                .arg(format!("--vm_name={}", vm_name))
                .arg(format!("--owner_id={}", user_id_hash))
                .args(&[
                    "--",
                    "LXD_DIR=/mnt/stateful/lxd",
                    "LXD_CONF=/mnt/stateful/lxd_conf",
                ])
                .status()?;
            Ok(())
        }
    }

    fn vsh_exec_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
    ) -> Result<(), Box<Error>> {
        Command::new("vsh")
            .arg(format!("--vm_name={}", vm_name))
            .arg(format!("--owner_id={}", user_id_hash))
            .arg(format!("--target_container={}", container_name))
            .args(&[
                "--",
                "LXD_DIR=/mnt/stateful/lxd",
                "LXD_CONF=/mnt/stateful/lxd_conf",
            ])
            .status()?;

        Ok(())
    }

    fn disk_destroy(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.destroy_disk_image(vm_name, user_id_hash)
    }

    fn disk_list(&mut self, user_id_hash: &str) -> Result<(Vec<(String, u64)>, u64), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let (images, total_size) = self.list_disk_images(user_id_hash, None, None)?;
        let out_images: Vec<(String, u64)> = images.into_iter().map(|e| (e.name, e.size)).collect();
        Ok((out_images, total_size))
    }

    fn disk_op_status(
        &mut self,
        uuid: &str,
        user_id_hash: &str,
    ) -> Result<(bool, u32), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.check_disk_operation(uuid)
    }

    fn wait_disk_op(&mut self, uuid: &str, user_id_hash: &str) -> Result<(bool, u32), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.wait_disk_operation(uuid)
    }

    fn container_create(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        image_server: &str,
        image_alias: &str,
    ) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.create_container(
            vm_name,
            user_id_hash,
            container_name,
            image_server,
            image_alias,
        )
    }

    fn container_start(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
    ) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.start_container(vm_name, user_id_hash, container_name)
    }

    fn container_setup_user(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        username: &str,
    ) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.setup_container_user(vm_name, user_id_hash, container_name, username)
    }

    fn usb_attach(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        bus: u8,
        device: u8,
    ) -> Result<u8, Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let usb_file_path = format!("/dev/bus/usb/{:03}/{:03}", bus, device);
        let usb_fd = self.permission_broker_open_path(Path::new(&usb_file_path))?;
        self.attach_usb(vm_name, user_id_hash, bus, device, usb_fd)
    }

    fn usb_detach(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        port: u8,
    ) -> Result<(), Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.detach_usb(vm_name, user_id_hash, port)
    }

    fn usb_list(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<Vec<(u8, u16, u16, String)>, Box<Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let device_list = self
            .list_usb(vm_name, user_id_hash)?
            .into_iter()
            .map(|mut d| {
                (
                    d.get_guest_port() as u8,
                    d.get_vendor_id() as u16,
                    d.get_product_id() as u16,
                    d.take_device_name(),
                )
            })
            .collect();
        Ok(device_list)
    }
}

fn is_stable_channel() -> bool {
    match LsbRelease::gather() {
        Ok(lsb) => lsb.release_channel() == Some(ReleaseChannel::Stable),
        Err(_) => {
            // Weird /etc/lsb-release, do not enforce stable restrictions.
            false
        }
    }
}
