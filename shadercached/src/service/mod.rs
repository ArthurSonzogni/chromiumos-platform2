// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mount;

use crate::common::*;
use crate::shader_cache_mount::*;
use mount::*;

use anyhow::Result;
use dbus::nonblock::SyncConnection;
use dbus::MethodErr;
use libchromeos::sys::{debug, info, warn};
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;
use system_api::concierge_service::{GetVmGpuCachePathRequest, GetVmGpuCachePathResponse};
use system_api::dlcservice::DlcState;
use system_api::shadercached::{InstallRequest, UninstallRequest};

// Path within gpu cache directory that is used for render server cache
const GPU_RENDER_SERVER_PATH: &str = "render_server";

pub async fn handle_install(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) -> Result<(), MethodErr> {
    let request: InstallRequest = protobuf::Message::parse_from_bytes(&raw_bytes)
        .map_err(|e| dbus::MethodErr::invalid_arg(&e))?;

    info!("Installing shader cache for {}", request.steam_app_id);

    // Populate mount path before installation to ensure mount happens
    if request.mount {
        let vm_id = VmId {
            vm_name: request.vm_name,
            vm_owner_id: request.vm_owner_id,
        };

        let mut mut_mount_map = mount_map.write().await;

        if !mut_mount_map.contains_key(&vm_id) {
            // We join render server path here because render server path is
            // what mesa sees as its base path, so we don't need to worry about
            // paths before that.
            let render_server_path = Path::new(
                &get_vm_gpu_cache_path(&vm_id, conn.clone())
                    .await
                    .map_err(to_method_err)?,
            )
            .join(GPU_RENDER_SERVER_PATH);

            mut_mount_map.insert(
                vm_id.clone(),
                ShaderCacheMount::new(&render_server_path, request.steam_app_id)
                    .map_err(to_method_err)?,
            );
        } else {
            debug!("Reusing mount information for {:?}", vm_id);
        }

        let shader_cache_mount = mut_mount_map
            .get_mut(&vm_id)
            .ok_or_else(|| MethodErr::failed("Failed to get mount information"))?;

        shader_cache_mount
            .setup_mount_destination(&vm_id, conn.clone())
            .await
            .map_err(to_method_err)?;

        // Even if current application is mounted, re-install and re-mount
        shader_cache_mount.mounted = false;
        shader_cache_mount.target_steam_app_id = request.steam_app_id;

        info!(
            "Mounting application {} to {:?} on successful installation",
            request.steam_app_id, vm_id
        );
    }

    install_shader_cache_dlc(request.steam_app_id, conn.clone())
        .await
        .map_err(to_method_err)?;

    Ok(())
}

pub async fn handle_uninstall(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) -> Result<(), MethodErr> {
    let request: UninstallRequest = protobuf::Message::parse_from_bytes(&raw_bytes)
        .map_err(|e| dbus::MethodErr::invalid_arg(&e))?;

    info!("Uninstall called for app: {}", request.steam_app_id);
    unmount_dlc(request.steam_app_id, mount_map, conn.clone())
        .await
        .map_err(to_method_err)?;
    uninstall_shader_cache_dlc(request.steam_app_id, conn.clone())
        .await
        .map_err(to_method_err)?;
    Ok(())
}

async fn uninstall_shader_cache_dlc(
    steam_game_id: SteamAppId,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let dlcservice_proxy = dbus::nonblock::Proxy::new(
        dlc_service::SERVICE_NAME,
        dlc_service::PATH_NAME,
        Duration::from_millis(5000),
        conn,
    );

    let dlc_name = steam_app_id_to_dlc(steam_game_id);

    info!("Requesting to uninstall {}", dlc_name);
    dlcservice_proxy
        .method_call(
            dlc_service::INTERFACE_NAME,
            dlc_service::UNINSTALL_METHOD,
            (dlc_name,),
        )
        .await?;
    Ok(())
}

async fn install_shader_cache_dlc(
    steam_game_id: SteamAppId,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let dlcservice_proxy = dbus::nonblock::Proxy::new(
        dlc_service::SERVICE_NAME,
        dlc_service::PATH_NAME,
        Duration::from_millis(5000),
        conn,
    );

    let dlc_name = steam_app_id_to_dlc(steam_game_id);

    info!("Requesting to install {}", dlc_name);
    dlcservice_proxy
        .method_call(
            dlc_service::INTERFACE_NAME,
            dlc_service::INSTALL_METHOD,
            (dlc_name,),
        )
        .await?;

    Ok(())
}

async fn get_vm_gpu_cache_path(vm_id: &VmId, conn: Arc<SyncConnection>) -> Result<String> {
    let concierge_proxy = dbus::nonblock::Proxy::new(
        vm_concierge::SERVICE_NAME,
        vm_concierge::PATH_NAME,
        Duration::from_millis(5000),
        conn.clone(),
    );
    let mut request = GetVmGpuCachePathRequest::new();
    request.set_name(vm_id.vm_name.to_owned());
    request.set_owner_id(vm_id.vm_owner_id.to_owned());
    let request_bytes = protobuf::Message::write_to_bytes(&request)?;

    // No autogenerated code for method calls (missing xml definition for
    // Concierge)
    let (response_bytes,): (Vec<u8>,) = concierge_proxy
        .method_call(
            vm_concierge::INTERFACE_NAME,
            vm_concierge::GET_VM_GPU_CACHE_PATH_METHOD,
            (request_bytes,),
        )
        .await?;

    let response: GetVmGpuCachePathResponse = protobuf::Message::parse_from_bytes(&response_bytes)?;

    Ok(response.get_path().to_owned())
}

pub async fn handle_dlc_state_changed(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) {
    // If shader cache DLC was installed, mount the DLC to a MountPoint that
    // wants this DLC.
    let dlc_state: DlcState = protobuf::Message::parse_from_bytes(&raw_bytes).unwrap();
    debug!(
        "DLC state changed: {}, {}",
        dlc_state.get_id(),
        dlc_state.get_progress()
    );
    if dlc_state.get_state() == system_api::dlcservice::DlcState_State::INSTALLED
        && dlc_state.get_progress() == 1.0
    {
        if let Ok(id) = dlc_to_steam_app_id(dlc_state.get_id()) {
            info!("ShaderCache DLC for {} installed, mounting if required", id);
            if let Err(e) = mount_dlc(id, mount_map, conn).await {
                warn!("Mount failed: {}", e);
            }
        }
    }
}

pub async fn handle_purge(
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) -> Result<(), MethodErr> {
    unmount_all(mount_map).await.map_err(to_method_err)?;
    let dlcservice_proxy = dbus::nonblock::Proxy::new(
        dlc_service::SERVICE_NAME,
        dlc_service::PATH_NAME,
        Duration::from_millis(5000),
        conn.clone(),
    );
    let (installed_ids,): (Vec<String>,) = dlcservice_proxy
        .method_call(
            dlc_service::INTERFACE_NAME,
            dlc_service::GET_INSTALLED_METHOD,
            (),
        )
        .await?;
    for dlc_id in installed_ids {
        if let Ok(steam_game_id) = dlc_to_steam_app_id(&dlc_id) {
            uninstall_shader_cache_dlc(steam_game_id, conn.clone())
                .await
                .map_err(to_method_err)?;
        }
    }
    Ok(())
}

pub async fn clean_up(mount_map: ShaderCacheMountMap) {
    debug!("Unmounting all");
    // Ignore unmount_all errors here, clean_up is only called on exit.
    let _ = unmount_all(mount_map).await;
}
