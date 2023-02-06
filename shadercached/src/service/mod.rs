// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// service module provides code entry path from D-BUS. Hence, functions here
// are naturally mapped to D-BUS methods.
// D-BUS methods and signals for other services (ex. concierge) are in
// individual modules.

mod concierge;
mod dlc;
pub mod signal;

use crate::common::*;
use crate::shader_cache_mount::{ShaderCacheMount, ShaderCacheMountMapPtr, VmId};

use anyhow::{anyhow, Result};
use dbus::{nonblock::SyncConnection, MethodErr};
use libchromeos::sys::debug;
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;
use system_api::shadercached::{InstallRequest, UninstallRequest, UnmountRequest};

// Selectively expose service methods
pub use concierge::add_shader_cache_group_permission;
pub use concierge::handle_vm_stopped;
pub use dlc::handle_dlc_state_changed;

pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

pub async fn handle_install(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let request: InstallRequest = protobuf::Message::parse_from_bytes(&raw_bytes)?;
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

        if !mut_mount_map.contains_key(&vm_id) {
            let vm_gpu_cache_path =
                Path::new(&concierge::get_vm_gpu_cache_path(&vm_id, conn.clone()).await?)
                    .to_path_buf();
            mut_mount_map.insert(vm_id.clone(), ShaderCacheMount::new(vm_gpu_cache_path)?);
        } else {
            debug!("Reusing mount information for {:?}", vm_id);
        }

        let shader_cache_mount = mut_mount_map
            .get_mut(&vm_id)
            .ok_or_else(|| MethodErr::failed("Failed to get mount information"))?;

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

// TODO(b/270617399): Make Purge accept VmId in the request proto once we
// support non-DLC precompiled caches
pub async fn handle_purge(
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    mount_map.clear_all_mounts(None).await?;
    mount_map
        .wait_unmount_completed(None, UNMOUNTER_INTERVAL * 2)
        .await?;
    // TODO(b/270262568): Queue DLC uninstallations instead of waiting for
    // unmounts.
    dlc::uninstall_all_shader_cache_dlcs(conn.clone()).await?;
    Ok(())
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
