// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{collections::HashSet, path::Path, sync::Arc};

use anyhow::{Context, Result};
use libchromeos::sys::debug;
use rand::Rng;
use tempfile::TempDir;

use crate::{
    common::{
        steam_app_id_to_dlc, SteamAppId, CRYPTO_HOME, GPU_DEVICE_ID, IMAGE_LOADER,
        PRECOMPILED_CACHE_DIR,
    },
    shader_cache_mount::{ShaderCacheMount, ShaderCacheMountMap, VmId},
};

lazy_static! {
    pub static ref MESA_VERSION_HASH: String = {
        let random_bytes: u128 = rand::thread_rng().gen();
        format!("{:x}", random_bytes)
    };
}

// TODO(endlesspring): organize the helper functions into modules or structs
// to improve readability.

fn create_mock_mesa_shader_cache_sf(base_dir: &Path) -> Result<()> {
    let mesa_path = base_dir
        .join("mesa_shader_cache_sf")
        .join(&*MESA_VERSION_HASH)
        .join(format!("anv_{:04x}", *GPU_DEVICE_ID));
    std::fs::create_dir_all(&mesa_path).context("Failed to create mesa_shader_cache_sf")?;
    std::fs::write(mesa_path.join("index"), "").context("Failed to create empty index file")?;
    std::fs::write(mesa_path.join("foz_cache.foz"), "")
        .context("Failed to create empty foz_cache.foz file")?;
    std::fs::write(mesa_path.join("foz_cache_idx.foz"), "")
        .context("Failed to create empty foz_cache_idx.foz file")?;

    debug!("Created mock mesa_shader_cache_sf at {:?}", mesa_path);

    Ok(())
}

pub fn generate_mount_list(mock_gpu_cache: &TempDir, steam_app_id: SteamAppId) -> String {
    format!(
        "/some/path on {} (some,options)\n",
        mock_gpu_cache
            .path()
            .join("render_server")
            .join("mesa_shader_cache_sf")
            .join(&*MESA_VERSION_HASH)
            .join(format!("anv_{:04x}", *GPU_DEVICE_ID))
            .join(steam_app_id.to_string())
            .to_str()
            .unwrap()
    )
}

pub fn mock_gpucache() -> Result<TempDir> {
    let mock_gpu_cache = tempfile::tempdir()?;
    let render_server_path = mock_gpu_cache.path().join("render_server");
    std::fs::create_dir_all(&render_server_path).context("Failed to create render_server")?;
    std::fs::File::create(render_server_path.join("foz_db_list.txt"))?;

    create_mock_mesa_shader_cache_sf(&render_server_path)?;

    Ok(mock_gpu_cache)
}

pub fn mock_shader_cache_dlc() -> Result<SteamAppId> {
    let random_id: SteamAppId = rand::thread_rng().gen();
    debug!("Generating shader cache dlc for {:?}", random_id);

    let base_dir = IMAGE_LOADER
        .join(steam_app_id_to_dlc(random_id))
        .join("package/root");
    create_mock_mesa_shader_cache_sf(&base_dir)?;
    Ok(random_id)
}

pub fn clean_up_mock_shader_cache_dlc(steam_app_id: SteamAppId) -> Result<()> {
    std::fs::remove_dir_all(IMAGE_LOADER.join(steam_app_id_to_dlc(steam_app_id)))?;
    Ok(())
}

pub fn foz_db_list_contains(mock_gpu_cache: &TempDir, game_id: SteamAppId) -> Result<bool> {
    let foz_db_list_contents =
        std::fs::read_to_string(mock_gpu_cache.path().join("render_server/foz_db_list.txt"))?;
    debug!("Foz db list contents {}", foz_db_list_contents);
    Ok(foz_db_list_contents
        .trim()
        .contains(&format!("{}/foz_cache", game_id)))
}

pub fn foz_db_list_empty(mock_gpu_cache: &TempDir) -> Result<bool> {
    let foz_db_list_contents =
        std::fs::read_to_string(mock_gpu_cache.path().join("render_server/foz_db_list.txt"))?;
    Ok(foz_db_list_contents.trim().is_empty())
}

pub async fn add_shader_cache_mount(
    mock_gpu_cache: &TempDir,
    mount_map: Arc<ShaderCacheMountMap>,
    vm_id: &VmId,
) -> Result<()> {
    let mut mount_map_write = mount_map.write().await;
    let mut shader_cache_mount = ShaderCacheMount::new(mock_gpu_cache.path().to_path_buf(), vm_id)?;
    shader_cache_mount.initialize()?;
    mount_map_write.insert(vm_id.clone(), shader_cache_mount);

    Ok(())
}

pub async fn enqueue_mount(
    mount_map: Arc<ShaderCacheMountMap>,
    vm_id: &VmId,
    steam_app_id: SteamAppId,
) -> Result<()> {
    let mut mount_map_write = mount_map.write().await;
    let shader_cache_mount = mount_map_write.get_mut(vm_id).unwrap();
    shader_cache_mount.enqueue_mount(steam_app_id);
    drop(mount_map_write);
    Ok(())
}

pub async fn get_mount_queue(
    mount_map: Arc<ShaderCacheMountMap>,
    vm_id: &VmId,
) -> Result<HashSet<SteamAppId>> {
    let mount_map_read = mount_map.read().await;
    let shader_cache_mount = mount_map_read.get(vm_id).unwrap();
    let cloned_queue = shader_cache_mount.get_mount_queue().clone();
    drop(mount_map_read);
    Ok(cloned_queue)
}

pub async fn get_unmount_queue(
    mount_map: Arc<ShaderCacheMountMap>,
    vm_id: &VmId,
) -> Result<HashSet<SteamAppId>> {
    let mount_map_read = mount_map.read().await;
    let shader_cache_mount = mount_map_read.get(vm_id).unwrap();
    let cloned_queue = shader_cache_mount.get_unmount_queue().clone();
    drop(mount_map_read);
    Ok(cloned_queue)
}

pub async fn simulate_mounted(mock_gpu_cache: &TempDir, steam_app_id: SteamAppId) -> Result<()> {
    let foz_db_list_file = mock_gpu_cache.path().join("render_server/foz_db_list.txt");
    let mut contents = std::fs::read_to_string(&foz_db_list_file)?;
    contents += &format!("{}/foz_cache", steam_app_id);
    contents += "\n";
    std::fs::write(foz_db_list_file, contents)?;

    let mesa_path = mock_gpu_cache
        .path()
        .join("render_server/mesa_shader_cache_sf")
        .join(&*MESA_VERSION_HASH)
        .join(format!("anv_{:04x}", *GPU_DEVICE_ID));
    std::fs::create_dir(mesa_path.join(steam_app_id.to_string()))?;

    Ok(())
}

pub async fn mount_destination_exists(
    mock_gpu_cache: &TempDir,
    steam_app_id: SteamAppId,
) -> Result<bool> {
    let foz_db_list_file = mock_gpu_cache.path().join("render_server/foz_db_list.txt");
    let mut contents = std::fs::read_to_string(&foz_db_list_file)?;
    contents += &format!("{}/foz_cache", steam_app_id);
    contents += "\n";
    std::fs::write(foz_db_list_file, contents)?;

    let mesa_path = mock_gpu_cache
        .path()
        .join("render_server/mesa_shader_cache_sf")
        .join(&*MESA_VERSION_HASH)
        .join(format!("anv_{:04x}", *GPU_DEVICE_ID))
        .join(steam_app_id.to_string());

    Ok(mesa_path.exists())
}

pub fn populate_precompiled_cache(vm_ids: &[&VmId]) -> Result<()> {
    let random_id: SteamAppId = rand::thread_rng().gen();
    // clean up cryptohome
    for path in (std::fs::read_dir(&*CRYPTO_HOME)?).flatten() {
        std::fs::remove_dir_all(path.path())?;
    }
    for vm_id in vm_ids {
        let encoded_vm_name = base64::encode_config(&vm_id.vm_name, base64::URL_SAFE);
        let base_path = CRYPTO_HOME
            .join(&vm_id.vm_owner_id)
            .join(PRECOMPILED_CACHE_DIR)
            .join(encoded_vm_name)
            .join(&random_id.to_string());
        std::fs::create_dir_all(base_path.join("fozpipelinesv6"))?;
        std::fs::write(
            base_path.join("fozpipelinesv6").join("replay-cache.foz"),
            "",
        )?;
        std::fs::create_dir_all(base_path.join("DXVK_state_cache"))?;
        std::fs::create_dir_all(base_path.join("fozmediaav1"))?;
    }
    Ok(())
}
