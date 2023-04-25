// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// All interactions with dlcservice is wrapped here. This includes both
// sending D-BUS methods and responding to signals.

use super::{signal, DEFAULT_DBUS_TIMEOUT};
use crate::common::*;
use crate::dbus_constants::dlc_service;
use crate::shader_cache_mount::ShaderCacheMountMapPtr;

use anyhow::{anyhow, Result};
use dbus::nonblock::SyncConnection;
use libchromeos::sys::{debug, info, warn};
use std::sync::Arc;
use system_api::{dlcservice::DlcState, shadercached::ShaderCacheMountStatus};

pub async fn handle_dlc_state_changed(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) {
    // If shader cache DLC was installed, mount the DLC to a MountPoint that
    // wants this DLC.
    let dlc_state: DlcState = protobuf::Message::parse_from_bytes(&raw_bytes).unwrap();
    debug!(
        "DLC state changed: {}, {}",
        dlc_state.id, dlc_state.progress
    );
    if dlc_state.state.enum_value() == Ok(system_api::dlcservice::dlc_state::State::INSTALLED)
        && dlc_state.progress == 1.0
    {
        if let Ok(id) = dlc_to_steam_app_id(&dlc_state.id) {
            info!("DLC state changed for shader cache DLC");
            debug!("ShaderCache DLC for {} installed, mounting if required", id);
            if let Err(e) = mount_dlc(id, mount_map, conn).await {
                warn!("Mount failed, {}", e);
            }
        }
    }
}

async fn mount_dlc(
    steam_app_id: SteamAppId,
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    info!("Mounting DLC");
    // Iterate through all mount points then attempt to mount shader cache if
    // |target_steam_app_id| matches |steam_app_id_to_mount| (which was just
    // installed)
    let mut mount_map = mount_map.write().await;
    let mut errors: Vec<String> = vec![];

    for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
        if shader_cache_mount.is_pending_mount(&steam_app_id) {
            debug!("Mounting for {:?}", vm_id);
            let mount_result = shader_cache_mount
                .setup_mount_destination(vm_id, steam_app_id, conn.clone())
                .await
                .and_then(|_| shader_cache_mount.bind_mount_dlc(steam_app_id))
                .and_then(|_| shader_cache_mount.add_game_to_db_list(steam_app_id));

            let mut mount_status = ShaderCacheMountStatus::new();
            mount_status.mounted = mount_result.is_ok();
            mount_status.vm_name = vm_id.vm_name.clone();
            mount_status.vm_owner_id = vm_id.vm_owner_id.clone();
            mount_status.steam_app_id = steam_app_id;
            if let Err(e) = mount_result {
                errors.push(format!("Failed to mount {:?}, {:?}", vm_id, e));
                mount_status.error = e.to_string();
            }
            if let Err(e) = signal::signal_mount_status(vec![mount_status], &conn) {
                errors.push(format!("Failed to send mount signal, {:?}\n", e));
            }
        }
    }

    if errors.is_empty() {
        Ok(())
    } else {
        Err(anyhow!("{:?}", errors))
    }
}

pub async fn unmount_dlc(
    steam_app_id_to_unmount: SteamAppId,
    mount_map: ShaderCacheMountMapPtr,
) -> Result<()> {
    info!("Unmounting DLC");
    // Iterate through all mount points then queue unmount for
    // |steam_app_id_to_unmount|
    {
        // |mount_map| with write mutex needs to go out of scope after this
        // loop so that background unmounter can take the mutex
        let mut mount_map = mount_map.write().await;
        for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
            debug!(
                "Processing DLC {} unmount for VM {:?}",
                steam_app_id_to_unmount, vm_id
            );
            shader_cache_mount.remove_game_from_db_list(steam_app_id_to_unmount)?;
        }
    }

    Ok(())
}

pub async fn install_shader_cache_dlc(
    steam_game_id: SteamAppId,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let dlcservice_proxy = dbus::nonblock::Proxy::new(
        dlc_service::SERVICE_NAME,
        dlc_service::PATH_NAME,
        DEFAULT_DBUS_TIMEOUT,
        conn,
    );

    let dlc_name = steam_app_id_to_dlc(steam_game_id);

    debug!("Requesting to install dlc {}", dlc_name);
    dlcservice_proxy
        .method_call(
            dlc_service::INTERFACE_NAME,
            dlc_service::INSTALL_METHOD,
            (dlc_name,),
        )
        .await?;

    Ok(())
}

pub async fn uninstall_shader_cache_dlc(
    steam_game_id: SteamAppId,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let dlcservice_proxy = dbus::nonblock::Proxy::new(
        dlc_service::SERVICE_NAME,
        dlc_service::PATH_NAME,
        DEFAULT_DBUS_TIMEOUT,
        conn,
    );

    let dlc_name = steam_app_id_to_dlc(steam_game_id);

    debug!("Requesting to uninstall dlc {}", dlc_name);
    dlcservice_proxy
        .method_call(
            dlc_service::INTERFACE_NAME,
            dlc_service::UNINSTALL_METHOD,
            (dlc_name,),
        )
        .await?;
    Ok(())
}

pub async fn uninstall_all_shader_cache_dlcs(conn: Arc<SyncConnection>) -> Result<()> {
    let dlcservice_proxy = dbus::nonblock::Proxy::new(
        dlc_service::SERVICE_NAME,
        dlc_service::PATH_NAME,
        DEFAULT_DBUS_TIMEOUT,
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
            uninstall_shader_cache_dlc(steam_game_id, conn.clone()).await?;
        }
    }

    Ok(())
}
