// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// service module provides code entry path from D-BUS. Hence, functions here
// are naturally mapped to D-BUS methods.
// D-BUS methods and signals for other services (ex. concierge) are in
// individual modules.

mod concierge;
mod dlc;
pub mod helper;
pub mod signal;
mod spaced;

use crate::common::*;
use crate::shader_cache_mount::{ShaderCacheMount, ShaderCacheMountMapPtr, VmId};
use helper::unsafe_quota::set_quota_normal;

use anyhow::{anyhow, Result};
use dbus::{nonblock::SyncConnection, MethodErr};
use libchromeos::sys::{debug, warn};
use protobuf::Message;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use system_api::shadercached::{
    InstallRequest, PrepareShaderCacheRequest, PrepareShaderCacheResponse, PurgeRequest,
    UninstallRequest, UnmountRequest,
};

// Selectively expose service methods
pub use concierge::add_shader_cache_group_permission;
pub use concierge::handle_vm_stopped;
pub use dlc::handle_dlc_state_changed;
pub use spaced::handle_disk_space_update;

pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

pub async fn handle_install(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let request: InstallRequest = Message::parse_from_bytes(&raw_bytes)?;
    // Populate mount path before installation to ensure mount happens
    if request.mount {
        debug!(
            "Install and mount requested for game {}",
            request.steam_app_id
        );
        let vm_id = VmId {
            vm_name: request.vm_name,
            vm_owner_id: request.vm_owner_id,
        };

        let mut mut_mount_map = mount_map.write().await;

        // TODO(b/271776528): move the insert ShaderCacheMount to
        // PrepareShaderCache method response once we support local RW shader
        // cache in shadercached.
        if !mut_mount_map.contains_key(&vm_id) {
            let vm_gpu_cache_path =
                Path::new(&concierge::get_vm_gpu_cache_path(&vm_id, conn.clone()).await?)
                    .to_path_buf();
            mut_mount_map.insert(
                vm_id.clone(),
                ShaderCacheMount::new(vm_gpu_cache_path, &vm_id)?,
            );
        } else {
            debug!("Reusing mount information for {:?}", vm_id);
        }

        let shader_cache_mount = mut_mount_map
            .get_mut(&vm_id)
            .ok_or_else(|| MethodErr::failed("Failed to get mount information"))?;

        // Mesa cache path initialization must succeed before we enqueue mount
        shader_cache_mount.initialize()?;
        // Even if current application is mounted, re-install and re-mount
        shader_cache_mount.enqueue_mount(request.steam_app_id);
    }

    dlc::install_shader_cache_dlc(request.steam_app_id, conn.clone()).await?;

    Ok(())
}

pub async fn handle_uninstall(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let request: UninstallRequest = protobuf::Message::parse_from_bytes(&raw_bytes)?;

    // Instead of queueing unmount, we attempt to unmount directly here.
    // Uninstall should only succeed if umounting succeeds immediately
    // (ie. nothing is using this game's shader cache DLC).
    dlc::unmount_dlc(request.steam_app_id, mount_map.clone()).await?;
    mount_map
        .wait_unmount_completed(Some(request.steam_app_id), UNMOUNTER_INTERVAL * 2)
        .await?;
    // TODO(b/270262568): Queue DLC uninstallations instead of waiting for
    // unmounts.
    dlc::uninstall_shader_cache_dlc(request.steam_app_id, conn.clone()).await?;
    Ok(())
}

pub async fn unmount_and_uninstall_all_shader_cache_dlcs(
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    mount_map.clear_all_mounts(None).await?;
    mount_map
        .wait_unmount_completed(None, UNMOUNTER_INTERVAL * 2)
        .await?;

    // TODO(b/270262568): Queue DLC uninstallations instead of waiting for
    // unmounts. Ensure to disallow mounts during the period and allow mounts
    // after completed.
    dlc::uninstall_all_shader_cache_dlcs(conn.clone()).await?;

    Ok(())
}

pub async fn handle_purge(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let request: PurgeRequest = protobuf::Message::parse_from_bytes(&raw_bytes)?;

    let mount_map_size = { mount_map.clone().read().await.len() };
    if mount_map_size <= 1 {
        // If only one active user, unmount for all users and uninstall all
        // DLCs. Shadercached does not keep state of users globally, hence
        // it cannot know if Borealis is still in use for other users. This
        // means another user may have to redownload the DLC, which is fine for
        // now.
        unmount_and_uninstall_all_shader_cache_dlcs(mount_map.clone(), conn.clone()).await?;
    }
    let vm_id = VmId {
        vm_name: request.vm_name,
        vm_owner_id: request.vm_owner_id,
    };
    let cleared_dirs = spaced::delete_precompiled_cache(mount_map.clone(), vm_id).await?;
    for dir_path in cleared_dirs {
        std::fs::remove_dir(dir_path)?;
    }
    Ok(())
}

pub async fn handle_prepare_shader_cache(
    raw_bytes: Vec<u8>,
    _mount_map: ShaderCacheMountMapPtr,
) -> Result<std::vec::Vec<u8>> {
    let request: PrepareShaderCacheRequest = protobuf::Message::parse_from_bytes(&raw_bytes)?;
    let mut response = PrepareShaderCacheResponse::new();
    if request.vm_name.is_empty() || request.vm_owner_id.is_empty() {
        return Err(anyhow!("vm_name and vm_owner_id must be set"));
    }

    let vm_id = VmId {
        vm_name: request.vm_name,
        vm_owner_id: request.vm_owner_id,
    };
    debug!("Preparing shader cache for {:?}", vm_id);

    // TODO(b/271776528): insert ShaderCacheMount here once we support local RW
    // shader cache in shadercached.
    let shader_cache_mount = ShaderCacheMount::new(PathBuf::new(), &vm_id)?;
    let path = shader_cache_mount.local_precompiled_cache_path()?;
    if !Path::new(&path).exists() {
        std::fs::create_dir_all(&path)?;
    }
    if let Err(e) = set_quota_normal(&path) {
        warn!("Failed to set quota for {}: {}", path, e);
    }
    response.precompiled_cache_path = path;

    Ok(response.write_to_bytes()?)
}

pub async fn handle_unmount(raw_bytes: Vec<u8>, mount_map: ShaderCacheMountMapPtr) -> Result<()> {
    let request: UnmountRequest = protobuf::Message::parse_from_bytes(&raw_bytes)?;
    let vm_id = VmId {
        vm_name: request.vm_name,
        vm_owner_id: request.vm_owner_id,
    };

    let mut mount_map = mount_map.write().await;
    if let Some(shader_cache_mount) = mount_map.get_mut(&vm_id) {
        // If VM had mounted something before but nothing is currently mounted,
        // unmount will be queued and unmount will succeed because there is
        // nothing mounted.
        // Shadercached will fire unmount signal in result, which can be
        // picked up by `garcon <..> --unmount --wait` process and exit
        // with success code.
        // (note: --wait flag probably only needed for tasts and manual
        // debugging).
        shader_cache_mount.remove_game_from_db_list(request.steam_app_id)?;
    } else {
        return Err(anyhow!("VM had never mounted shader cache"));
    }

    Ok(())
}
