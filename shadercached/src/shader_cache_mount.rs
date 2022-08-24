// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;

use anyhow::{anyhow, Result};
use dbus::nonblock::SyncConnection;
use std::collections::HashMap;
use std::ffi::OsString;
use std::fs;
use std::os::unix::prelude::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use sys_util::{debug, info};
use system_api::concierge_service::AddGroupPermissionMesaRequest;
use tokio::sync::RwLock;

pub type ShaderCacheMountMap = Arc<RwLock<HashMap<VmId, ShaderCacheMount>>>;

const GPU_CACHE_FINAL_MOUNT_DIR: &str = "dlc";

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct VmId {
    pub vm_name: String,
    pub vm_owner_id: String,
}

#[derive(Debug, Clone)]
pub struct ShaderCacheMount {
    pub mounted: bool,
    // The Steam application that we want to mount to this directory.
    pub target_steam_app_id: SteamAppId,
    // Absolute path to bind-mount DLC contents into.
    pub absolute_mount_destination_path: PathBuf,
    // Within gpu cache directory, mesa creates a nested sub directory to store
    // shader cache. |relative_mesa_cache_path| is relative to the
    // render_server's base path within crosvm's gpu cache directory
    relative_mesa_cache_path: PathBuf,
}

impl ShaderCacheMount {
    pub fn new(render_server_path: &Path, target_steam_app_id: u64) -> Result<ShaderCacheMount> {
        let relative_mesa_cache_path =
            get_mesa_cache_relative_path(render_server_path).map_err(to_method_err)?;
        let absolute_mount_destination_path = render_server_path
            .join(&relative_mesa_cache_path)
            .join(GPU_CACHE_FINAL_MOUNT_DIR);
        Ok(ShaderCacheMount {
            target_steam_app_id,
            mounted: false,
            relative_mesa_cache_path,
            absolute_mount_destination_path,
        })
    }

    pub fn get_str_absolute_mount_destination_path(&self) -> Result<&str> {
        self.absolute_mount_destination_path
            .as_os_str()
            .to_str()
            .ok_or_else(|| {
                anyhow!(
                    "Failed to get string path for {:?}",
                    self.absolute_mount_destination_path
                )
            })
    }

    pub fn dlc_content_path(&self) -> Result<String> {
        // Generate DLC content
        let str_base = format!(
            "/run/imageloader/borealis-shader-cache-{}-dlc/package/root/",
            self.target_steam_app_id
        );
        let path = Path::new(&str_base).join(&self.relative_mesa_cache_path);

        if !path.exists() {
            return Err(anyhow!(
                "No shader cache DLC for Steam app {}, expected path {:?}",
                self.target_steam_app_id,
                path.as_os_str()
            ));
        }

        path.into_os_string()
            .into_string()
            .map_err(|os_str| anyhow!("Failed to convert path to string: {:?}", os_str))
    }

    pub async fn setup_mount_destination(
        &self,
        vm_id: &VmId,
        conn: Arc<SyncConnection>,
    ) -> Result<()> {
        if !self.absolute_mount_destination_path.exists() {
            // Attempt to only create the final directory, the parent directory
            // should already exist.
            if let Err(e) = fs::create_dir(&self.absolute_mount_destination_path) {
                debug!(
                    "Failed create mount directory: {:?}, retrying after getting permissions",
                    e
                );
                // Retry directory creation once with permissions fix
                add_shader_cache_group_permission(vm_id, conn).await?;
                fs::create_dir(&self.absolute_mount_destination_path)?;
                debug!("Successfully created mount directory on retry");
            }
            let perm = fs::Permissions::from_mode(0o750);
            fs::set_permissions(&self.absolute_mount_destination_path, perm)?;
        }

        Ok(())
    }
}

pub fn new_mount_map() -> ShaderCacheMountMap {
    Arc::new(RwLock::new(HashMap::new()))
}

fn get_mesa_cache_relative_path(render_server_path: &Path) -> Result<PathBuf> {
    // Within gpu cache directory, mesa creates a nested sub directory to store
    // shader cache.
    // This function figures out this relative mount path from GPU cache dir:
    //   <GPU cache dir>/render_server/<mesa_cache_path>/
    // where mesa_cache_path is (usually):
    //   <mesa cache type>/<mesa_hash>/<gpu id from driver>
    // This mesa_cache_path has the actual binary cache blobs used by mesa,
    // along with the 'index' file.
    let mut absolute_path = Path::new(render_server_path).to_path_buf();
    let mut relative_path = PathBuf::new();

    while let Ok(path) = get_single_file(&absolute_path) {
        absolute_path = absolute_path.join(&path);
        relative_path = relative_path.join(&path);
    }

    // Under normal scenarios, index file will always be found since mesa
    // cache path is always created when Steam launches. However, on edge
    // cases (ex. manual installation call before Steam launch), mesa index file
    // may not exist.
    if !has_index_file(&absolute_path)? {
        return Err(anyhow!("Invalid mesa cache structure!"));
    }

    Ok(relative_path)
}

async fn add_shader_cache_group_permission(vm_id: &VmId, conn: Arc<SyncConnection>) -> Result<()> {
    let concierge_proxy = dbus::nonblock::Proxy::new(
        vm_concierge::SERVICE_NAME,
        vm_concierge::PATH_NAME,
        Duration::from_millis(5000),
        conn,
    );

    let mut request = AddGroupPermissionMesaRequest::new();
    request.set_name(vm_id.vm_name.to_owned());
    request.set_owner_id(vm_id.vm_owner_id.to_owned());
    let request_bytes = protobuf::Message::write_to_bytes(&request)?;

    info!("Requesting concierge to add group permission");
    concierge_proxy
        .method_call(
            vm_concierge::INTERFACE_NAME,
            vm_concierge::ADD_GROUP_PERMISSION_MESA_METHOD,
            (request_bytes,),
        )
        .await?;

    Ok(())
}

fn get_single_file(path: &Path) -> Result<OsString> {
    let mut entries = fs::read_dir(path)?;
    let entry = entries.next();
    if entries.next().is_some() {
        return Err(anyhow!("Multiple directories found under: {:?}", path));
    }
    match entry {
        Some(entry) => Ok(entry?.file_name()),
        None => Err(anyhow!("Empty directory: {:?}", path)),
    }
}

fn has_index_file(path: &Path) -> Result<bool> {
    let entries = fs::read_dir(path)?;
    for dir_entry in entries {
        if dir_entry?.file_name() == "index" {
            return Ok(true);
        }
    }
    Ok(false)
}

#[cfg(test)]
mod tests {
    use std::env;
    use std::fs;
    use std::path::Path;

    #[test]
    fn test_get_single_file() {
        let temp_dir = env::temp_dir().join("test_get_single_file");
        let _ = fs::remove_dir_all(temp_dir.as_path());

        // single directory present
        fs::create_dir_all(temp_dir.join(Path::new("child"))).unwrap();
        assert_eq!(
            super::get_single_file(temp_dir.as_path())
                .unwrap()
                .to_str()
                .unwrap(),
            "child"
        );

        // multiple directories present
        fs::create_dir(temp_dir.join(Path::new("child2"))).unwrap();
        assert!(super::get_single_file(temp_dir.as_path()).is_err());
    }
    #[test]
    fn test_steam_app_id_to_dlc() {
        assert_eq!(
            super::steam_app_id_to_dlc(32),
            "borealis-shader-cache-32-dlc"
        );
        assert_eq!(
            super::steam_app_id_to_dlc(123),
            "borealis-shader-cache-123-dlc"
        );
        assert_eq!(
            super::steam_app_id_to_dlc(0000),
            "borealis-shader-cache-0-dlc"
        );
    }

    #[test]
    fn test_dlc_to_steam_app_id() {
        assert_eq!(
            super::dlc_to_steam_app_id("borealis-shader-cache-32-dlc").unwrap(),
            32
        );
        assert_eq!(
            super::dlc_to_steam_app_id("borealis-shader-cache-000-dlc").unwrap(),
            0
        );
        assert!(super::dlc_to_steam_app_id("borealis-shader-cache-213").is_err());
        assert!(super::dlc_to_steam_app_id("213-dlc").is_err());
        assert!(super::dlc_to_steam_app_id("not-a-valid-one").is_err());
        assert!(super::dlc_to_steam_app_id("borealis-dlc").is_err());
        assert!(super::dlc_to_steam_app_id("borealis-shader-cache-two-dlc").is_err());
    }
}
