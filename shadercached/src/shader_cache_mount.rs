// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;

use anyhow::{anyhow, Result};
use dbus::nonblock::SyncConnection;
use libchromeos::sys::{debug, error, info, warn};
use std::collections::{HashMap, HashSet};
use std::ffi::OsString;
use std::fs;
use std::os::unix::prelude::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use system_api::concierge_service::AddGroupPermissionMesaRequest;
use tokio::sync::RwLock;

pub type ShaderCacheMountMap = Arc<RwLock<HashMap<VmId, ShaderCacheMount>>>;

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
    // Steam app ids to unmount in periodic unmount loop
    pub unmount_queue: HashSet<SteamAppId>,
    // Absolute path to bind-mount DLC contents into.
    absolute_mount_destination_path: PathBuf,
    // Within gpu cache directory, mesa creates a nested sub directory to store
    // shader cache. |relative_mesa_cache_path| is relative to the
    // render_server's base path within crosvm's gpu cache directory
    relative_mesa_cache_path: PathBuf,
    // After mounting or before unmounting, we need to update foz db list file
    // so that mesa uses or stops using the path.
    foz_blob_db_list_path: PathBuf,
}

impl ShaderCacheMount {
    pub fn new(render_server_path: &Path, target_steam_app_id: u64) -> Result<ShaderCacheMount> {
        let relative_mesa_cache_path =
            get_mesa_cache_relative_path(render_server_path).map_err(to_method_err)?;
        let absolute_mount_destination_path = render_server_path.join(&relative_mesa_cache_path);
        Ok(ShaderCacheMount {
            target_steam_app_id,
            mounted: false,
            relative_mesa_cache_path,
            absolute_mount_destination_path,
            unmount_queue: HashSet::new(),
            foz_blob_db_list_path: render_server_path.join(FOZ_DB_LIST_FILE),
        })
    }

    pub fn add_game_to_db_list(&self, steam_app_id: SteamAppId) -> Result<()> {
        // Add game to foz_db_list so that mesa can start using the directory
        // for precompiled cache. Adding game to list must happen after the
        // directory has been created and mounted.
        if !self.foz_blob_db_list_path.exists() {
            return Err(anyhow!("Missing foz blob file"));
        }

        let read_result = fs::read_to_string(&self.foz_blob_db_list_path);
        if let Err(e) = read_result {
            return Err(anyhow!("Failed to read contents: {}", e));
        }

        info!("Adding {} to foz db list", steam_app_id);
        let mut contents = read_result.unwrap();

        let entry_to_add = format!("{}/{}", steam_app_id, PRECOMPILED_CACHE_FILE_NAME);
        for line in contents.split('\n') {
            if line == entry_to_add {
                debug!("{} already in the entry", steam_app_id);
                return Ok(());
            }
        }

        contents += &entry_to_add;
        contents += "\n";

        fs::write(&self.foz_blob_db_list_path, contents)?;

        Ok(())
    }

    pub fn remove_game_from_db_list(&self, steam_app_id: SteamAppId) -> Result<bool> {
        // Remove game from foz_db_list so that mesa stops using the precompiled
        // cache in it. Removing the game from list must happen before
        // unmounting and removing the directory.
        if !self.foz_blob_db_list_path.exists() {
            return Err(anyhow!("Missing foz blob file"));
        }
        let read_result = fs::read_to_string(&self.foz_blob_db_list_path);
        if let Err(e) = read_result {
            return Err(anyhow!("Failed to read contents: {}", e));
        }

        info!("Removing {} from foz db list if it exists", steam_app_id);
        let contents = read_result.unwrap();
        let mut write_contents = String::new();

        let mut found = false;
        let entry_to_remove = format!("{}/{}", steam_app_id, PRECOMPILED_CACHE_FILE_NAME);
        for line in contents.split('\n') {
            // Even the final entry in foz blob db list file has new line, so
            // the last line in contents.split('\n') is empty string
            if line.is_empty() {
                continue;
            }
            if line != entry_to_remove {
                write_contents += line;
                write_contents += "\n";
            } else {
                found = true
            }
        }

        fs::write(&self.foz_blob_db_list_path, write_contents)?;

        Ok(found)
    }

    pub fn queue_unmount_all(&mut self) -> Result<()> {
        if !self.foz_blob_db_list_path.exists() {
            warn!(
                "Nothing to unmount, specified path does not exist: {:?}",
                self.foz_blob_db_list_path
            );
            return Ok(());
        }

        let mut cleared_games: HashSet<SteamAppId> = HashSet::new();
        let list_string = fs::read_to_string(&self.foz_blob_db_list_path)?;
        // Example foz db list file contents:
        // 620/foz_cache
        // 570/foz_cache
        //
        for relative_path in list_string.split('\n') {
            // first entry in the relative path is the steam app id
            if let Some(app_id_string) = relative_path.split('/').next() {
                cleared_games.insert(app_id_string.parse::<SteamAppId>()?);
            }
        }

        for entry in fs::read_dir(&self.absolute_mount_destination_path)? {
            let entry = entry?;
            if entry.path().is_dir() {
                if let Ok(str_entry) = entry.file_name().into_string() {
                    let found_id = str_entry.parse::<SteamAppId>()?;
                    if !cleared_games.contains(&found_id) {
                        debug!(
                            "Found unexpected precompiled cache mount for app {}, ignoring",
                            found_id
                        );
                    }
                }
            }
        }

        fs::write(&self.foz_blob_db_list_path, "")?;

        for game in cleared_games {
            self.queue_unmount(game)?;
        }

        Ok(())
    }

    pub fn queue_unmount(&mut self, steam_app_id: SteamAppId) -> Result<()> {
        // |steam_app_id_to_unmount| is iterated by background unmounter thread
        if !self.unmount_queue.insert(steam_app_id) {
            return Err(anyhow!("Inserting to hash set failed"));
        }
        Ok(())
    }

    pub fn get_str_absolute_mount_destination_path(
        &self,
        steam_app_id: SteamAppId,
    ) -> Result<String> {
        match self
            .absolute_mount_destination_path
            .join(steam_app_id.to_string())
            .to_str()
        {
            Some(str) => Ok(String::from(str)),
            None => Err(anyhow!(
                "Failed to get string path for {:?}",
                self.absolute_mount_destination_path
            )),
        }
    }

    pub fn dlc_content_path(&self) -> Result<String> {
        // Generate DLC content
        let str_base = format!(
            "/run/imageloader/{}/package/root/",
            steam_app_id_to_dlc(self.target_steam_app_id),
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
        steam_app_id: SteamAppId,
        conn: Arc<SyncConnection>,
    ) -> Result<()> {
        info!(
            "Setting up mount destination for {:?}, game {}",
            vm_id, steam_app_id
        );
        let dst_path_str = self.get_str_absolute_mount_destination_path(steam_app_id)?;
        let dst_path = Path::new(&dst_path_str);
        if !dst_path.exists() {
            // Attempt to only create the final directory, the parent directory
            // should already exist.
            if let Err(e) = fs::create_dir(&dst_path) {
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
            if let Err(e) = fs::set_permissions(&dst_path, perm) {
                error!("Failed to set permissions for {}: {}", dst_path_str, e);
                fs::remove_dir(dst_path)?;
                return Err(anyhow!("Failed to set permissions: {}", e));
            }
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
    //   mesa_shader_cache_sf/<mesa_hash>/anv_<gpu device id>
    // or (for AMD):
    //   mesa_shader_cache_sf/<mesa_hash>/<gpu generation name>
    // This mesa_cache_path has the actual binary cache blobs used by mesa,
    // along with the 'foz_blob.foz' and/or 'index' file.
    let mut absolute_path = Path::new(render_server_path)
        .to_path_buf()
        .join(MESA_SINGLE_FILE_DIR);
    let mut relative_path = Path::new(MESA_SINGLE_FILE_DIR).to_path_buf();

    info!("Getting mesa hash and device id path");

    let mesa_hash = get_single_file(&absolute_path)?;
    absolute_path = absolute_path.join(&mesa_hash);
    relative_path = relative_path.join(&mesa_hash);

    let device_id_path = get_single_file(&absolute_path)?;
    absolute_path = absolute_path.join(&device_id_path);
    relative_path = relative_path.join(&device_id_path);

    if !absolute_path.exists() {
        return Err(anyhow!(
            "{:?} does not exist, report GPU device ID may not match",
            absolute_path
        ));
    }

    // Mesa initializes the cache directory if there was a need to store things
    // into the cache: ex. any GUI app launch, like Steam.
    // When this happens, foz_blob.foz file will always be found.
    //
    // However, on edge cases (ex. manual installation call before Steam
    // launch), foz_blob.foz file may not exist.
    if !has_file(&absolute_path, FOZ_CACHE_FILE_NAME)?
        && !has_file(&absolute_path, INDEX_FILE_NAME)?
    {
        return Err(anyhow!(
            "Invalid mesa cache structure at {:?}",
            absolute_path
        ));
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

fn has_file(path: &Path, file_name: &str) -> Result<bool> {
    let entries = fs::read_dir(path)?;
    for dir_entry in entries {
        if dir_entry?.file_name() == file_name {
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
