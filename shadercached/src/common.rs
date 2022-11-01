// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use dbus::MethodErr;
use std::time::Duration;

pub type SteamAppId = u64;

pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

// TODO(b/247385169): get these from system_api c++ header instead
pub const SERVICE_NAME: &str = "org.chromium.ShaderCache";
pub const PATH_NAME: &str = "/org/chromium/ShaderCache";
pub const INTERFACE_NAME: &str = SERVICE_NAME;
pub const MOUNT_SIGNAL_NAME: &str = "ShaderCacheMountStatusChanged";
pub const INSTALL_METHOD: &str = "Install";
pub const UNINSTALL_METHOD: &str = "Uninstall";
pub const PURGE_METHOD: &str = "Purge";

// TODO(b/247385169): get these from system_api c++ header instead
pub mod dlc_service {
    pub const SERVICE_NAME: &str = "org.chromium.DlcService";
    pub const PATH_NAME: &str = "/org/chromium/DlcService";
    pub const INTERFACE_NAME: &str = "org.chromium.DlcServiceInterface";

    pub const INSTALL_METHOD: &str = "InstallDlc";
    pub const UNINSTALL_METHOD: &str = "Uninstall";
    pub const GET_INSTALLED_METHOD: &str = "GetInstalled";

    pub const DLC_STATE_CHANGED_SIGNAL: &str = "DlcStateChanged";
}

// TODO(b/247385169): get these from system_api c++ header instead
pub mod vm_concierge {
    pub const SERVICE_NAME: &str = "org.chromium.VmConcierge";
    pub const PATH_NAME: &str = "/org/chromium/VmConcierge";
    pub const INTERFACE_NAME: &str = "org.chromium.VmConcierge";

    pub const ADD_GROUP_PERMISSION_MESA_METHOD: &str = "AddGroupPermissionMesa";
    pub const GET_VM_GPU_CACHE_PATH_METHOD: &str = "GetVmGpuCachePath";
}

pub fn to_method_err<T: std::fmt::Display>(result: T) -> MethodErr {
    MethodErr::failed(&result)
}

pub fn steam_app_id_to_dlc(steam_app_id: SteamAppId) -> String {
    format!("borealis-shader-cache-{}-dlc", steam_app_id)
}

pub fn dlc_to_steam_app_id(dlc_name: &str) -> Result<SteamAppId> {
    if dlc_name.starts_with("borealis-shader-cache-") && dlc_name.ends_with("-dlc") {
        return dlc_name
            .replace("borealis-shader-cache-", "")
            .replace("-dlc", "")
            .parse::<SteamAppId>()
            .map_err(|e| anyhow!(e));
    }

    Err(anyhow!("Not a valid DLC"))
}
