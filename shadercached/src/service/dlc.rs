// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// All interactions with dlcservice is wrapped here. This includes both
// sending D-BUS methods and responding to signals.

use super::signal;
use crate::dbus_constants::dlc_service;
use crate::dbus_wrapper::DbusConnectionTrait;
use crate::shader_cache_mount::ShaderCacheMountMapPtr;
use crate::{common::*, dlc_queue::DlcQueuePtr};

use anyhow::{anyhow, Result};
use log::{debug, error, info, warn};
use std::collections::HashSet;
use std::sync::Arc;
use system_api::{
    dlcservice::dlc_state::State, dlcservice::DlcState, shadercached::ShaderCacheMountStatus,
};

pub async fn handle_dlc_state_changed<D: DbusConnectionTrait>(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
    dlc_queue: DlcQueuePtr,
    dbus_conn: Arc<D>,
) {
    // If shader cache DLC was installed, mount the DLC to a MountPoint that
    // wants this DLC.
    let dlc_state: DlcState = protobuf::Message::parse_from_bytes(&raw_bytes).unwrap();
    debug!(
        "DLC state changed: {}, {:?}, {}",
        dlc_state.id,
        dlc_state.state.enum_value(),
        dlc_state.progress
    );
    if let Ok(steam_app_id) = dlc_to_steam_app_id(&dlc_state.id) {
        // Note that INSTALLING and INSTALLED messages can be sent out of order
        // if the installation was very fast. Hence, INSTALLING state is
        // ignored. |dlc_queue.add_installing| is added during
        // |dlc_queue.next_to_install| inside |periodic_dlc_handler|.
        if dlc_state.state.enum_value() == Ok(State::INSTALLED) {
            info!("Shader cache DLC installed");
            let mut dlc_queue = dlc_queue.write().await;
            dlc_queue.remove_installing(&steam_app_id);
            debug!(
                "ShaderCache DLC for {} installed, mounting if required",
                steam_app_id
            );
            if let Err(e) = mount_dlc(steam_app_id, mount_map, dbus_conn.clone()).await {
                warn!("Mount failed, {}", e);
            }
        } else if dlc_state.state.enum_value() == Ok(State::NOT_INSTALLED) {
            info!("Shader cache DLC failed to install");
            debug!("Failed to install DLC for {}", steam_app_id);
            let mut dlc_queue = dlc_queue.write().await;
            if !dlc_queue.remove_installing(&steam_app_id) {
                warn!("DLC failed to install, but it was not found in installing set");
            }
            warn!("Clearing mount requests for the failed DLC");
            if let Err(e) = dequeue_mount_for_failed_dlc(
                steam_app_id,
                "dlc could not be installed",
                mount_map.clone(),
                dbus_conn.clone(),
            )
            .await
            {
                warn!("Failed to notify failed mounts {}", e);
            }
        } else if dlc_state.state.enum_value() == Ok(State::INSTALLING) {
            debug!("Shader cache DLC install progress: {}", dlc_state.progress);
        }
    }
}

async fn dequeue_mount_for_failed_dlc<D: DbusConnectionTrait>(
    steam_app_id: SteamAppId,
    error: &str,
    mount_map: ShaderCacheMountMapPtr,
    dbus_conn: Arc<D>,
) -> Result<()> {
    let mut mount_map = mount_map.write().await;
    let mut mount_status_to_send: Vec<ShaderCacheMountStatus> = vec![];

    for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
        if !shader_cache_mount.is_pending_mount(&steam_app_id) {
            continue;
        }

        shader_cache_mount.dequeue_mount(&steam_app_id);
        let mut mount_status = ShaderCacheMountStatus::new();
        mount_status.mounted = false;
        mount_status.vm_name = vm_id.vm_name.clone();
        mount_status.vm_owner_id = vm_id.vm_owner_id.clone();
        mount_status.steam_app_id = steam_app_id;
        mount_status.error = format!("Mount not attempted {:?}: {}", vm_id, error);
        mount_status_to_send.push(mount_status);
    }
    signal::signal_mount_status(mount_status_to_send, dbus_conn)
}

pub async fn periodic_dlc_handler<D: DbusConnectionTrait>(
    mount_map: ShaderCacheMountMapPtr,
    dlc_queue: DlcQueuePtr,
    dbus_conn: Arc<D>,
) {
    let mut dlc_queue = dlc_queue.write().await;
    debug!("{}", dlc_queue);

    if dlc_queue.count_installing_dlcs() < MAX_CONCURRENT_DLC_INSTALLS {
        // Handle install queue
        while let Some(steam_app_id) = dlc_queue.next_to_install() {
            debug!("{}", dlc_queue);
            let result = install_shader_cache_dlc(steam_app_id, dbus_conn.clone()).await;
            if result.is_ok() {
                debug!("Started installing shader cache for {}", steam_app_id);
                // Successfully queued install, stop trying
                break;
            }
            // Don't retry to install dlc again, there are retries from
            // the VM side in various points of UX.
            // Simply just remove from installing set and try next.
            dlc_queue.remove_installing(&steam_app_id);
            // If mounting was queued, remove it.
            if let Err(e) = dequeue_mount_for_failed_dlc(
                steam_app_id,
                "dlc is missing",
                mount_map.clone(),
                dbus_conn.clone(),
            )
            .await
            {
                error!("Failed to dequeue failed install: {}", e);
            }
            warn!("Failed to install shader cache dlc");
        }
    } else {
        debug!(
            "{} shader cache dlcs are being installed, not triggering more installs",
            dlc_queue.count_installing_dlcs()
        );
    }

    let mut failed_uninstalls: HashSet<SteamAppId> = HashSet::new();
    // Handle uninstall queue
    while let Some(steam_app_id) = dlc_queue.next_to_uninstall() {
        debug!("Uninstalling shader cache for {}", steam_app_id);
        if let Err(e) = unmount_dlc(steam_app_id, mount_map.clone()).await {
            warn!("Failed to unmount: {}", e);
            failed_uninstalls.insert(steam_app_id);
            continue;
        }
        if let Err(e) = mount_map
            .wait_unmount_completed(Some(steam_app_id), UNMOUNTER_INTERVAL * 2)
            .await
        {
            warn!("Failed to wait for unmount: {}", e);
            failed_uninstalls.insert(steam_app_id);
            continue;
        }
        if let Err(e) = uninstall_shader_cache_dlc(steam_app_id, dbus_conn.clone()).await {
            // DLC uninstallation can fail if DLC is missing or transient
            // failures.
            // TODO(b/285965527): Retry shader dlc uninstallation on dlc missing
            // failure
            warn!("Failed to uninstall DLC");
            debug!(
                "Failed to uninstall DLC: {}, not attempting to uninstall",
                e
            );
            continue;
        }
    }
    dlc_queue.queue_uninstall_multi(&failed_uninstalls);
}

pub async fn mount_dlc<D: DbusConnectionTrait>(
    steam_app_id: SteamAppId,
    mount_map: ShaderCacheMountMapPtr,
    dbus_conn: Arc<D>,
) -> Result<()> {
    info!("Mounting DLC");
    // Iterate through all mount points then attempt to mount shader cache if
    // |target_steam_app_id| matches |steam_app_id_to_mount| (which was just
    // installed)
    let mut mount_map = mount_map.write().await;
    let mut errors: Vec<String> = vec![];
    let mut mount_status_to_send: Vec<ShaderCacheMountStatus> = vec![];

    for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
        if shader_cache_mount.is_pending_mount(&steam_app_id) {
            debug!("Mounting for {:?}", vm_id);
            let mount_result = shader_cache_mount
                .setup_mount_destination(vm_id, steam_app_id)
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
            mount_status_to_send.push(mount_status);
        }
    }

    if let Err(e) = signal::signal_mount_status(mount_status_to_send, dbus_conn.clone()) {
        errors.push(e.to_string());
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

pub async fn install_shader_cache_dlc<D: DbusConnectionTrait>(
    steam_game_id: SteamAppId,
    dbus_conn: Arc<D>,
) -> Result<()> {
    let dlc_name = steam_app_id_to_dlc(steam_game_id);

    debug!("Requesting to install dlc {}", dlc_name);
    dbus_conn
        .call_dbus_method(
            dlc_service::SERVICE_NAME,
            dlc_service::PATH_NAME,
            dlc_service::INTERFACE_NAME,
            dlc_service::INSTALL_METHOD,
            (dlc_name,),
        )
        .await?;

    Ok(())
}

pub async fn uninstall_shader_cache_dlc<D: DbusConnectionTrait>(
    steam_game_id: SteamAppId,
    dbus_conn: Arc<D>,
) -> Result<()> {
    let dlc_name = steam_app_id_to_dlc(steam_game_id);
    debug!("Requesting to uninstall dlc {}", dlc_name);
    dbus_conn
        .call_dbus_method(
            dlc_service::SERVICE_NAME,
            dlc_service::PATH_NAME,
            dlc_service::INTERFACE_NAME,
            dlc_service::UNINSTALL_METHOD,
            (dlc_name,),
        )
        .await?;
    Ok(())
}

pub async fn uninstall_all_shader_cache_dlcs<D: DbusConnectionTrait>(
    dbus_conn: Arc<D>,
) -> Result<()> {
    let (installed_ids,): (Vec<String>,) = dbus_conn
        .call_dbus_method(
            dlc_service::SERVICE_NAME,
            dlc_service::PATH_NAME,
            dlc_service::INTERFACE_NAME,
            dlc_service::GET_INSTALLED_METHOD,
            (),
        )
        .await?;
    for dlc_id in installed_ids {
        if let Ok(steam_game_id) = dlc_to_steam_app_id(&dlc_id) {
            uninstall_shader_cache_dlc(steam_game_id, dbus_conn.clone()).await?;
        }
    }

    Ok(())
}
