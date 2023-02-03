// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mount;

use crate::common::*;
use crate::shader_cache_mount::*;
use mount::*;

use anyhow::{anyhow, Result};
use dbus::nonblock::SyncConnection;
use dbus::MethodErr;
use libchromeos::sys::{debug, error, info, warn};
use std::collections::HashSet;
use std::path::Path;
use std::sync::Arc;
use system_api::concierge_service::VmStoppingSignal;
use system_api::concierge_service::{GetVmGpuCachePathRequest, GetVmGpuCachePathResponse};
use system_api::dlcservice::DlcState;
use system_api::shadercached::{InstallRequest, UninstallRequest, UnmountRequest};

// Path within gpu cache directory that is used for render server cache
const GPU_RENDER_SERVER_PATH: &str = "render_server";

pub async fn handle_install(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) -> Result<(), MethodErr> {
    let request: InstallRequest = protobuf::Message::parse_from_bytes(&raw_bytes)
        .map_err(|e| dbus::MethodErr::invalid_arg(&e))?;
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

        // Even if current application is mounted, re-install and re-mount
        shader_cache_mount.enqueue_mount(request.steam_app_id);
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

    // Instead of queueing unmount, we attempt to unmount directly here.
    // Uninstall should only succeed if umounting succeeds immediately
    // (ie. nothing is using this game's shader cache DLC).
    unmount_dlc(request.steam_app_id, mount_map)
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

async fn install_shader_cache_dlc(
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

async fn get_vm_gpu_cache_path(vm_id: &VmId, conn: Arc<SyncConnection>) -> Result<String> {
    let concierge_proxy = dbus::nonblock::Proxy::new(
        vm_concierge::SERVICE_NAME,
        vm_concierge::PATH_NAME,
        DEFAULT_DBUS_TIMEOUT,
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
            info!("DLC state changed for shader cache DLC");
            debug!("ShaderCache DLC for {} installed, mounting if required", id);
            if let Err(e) = mount_dlc(id, mount_map, conn).await {
                warn!("Mount failed, {}", e);
            }
        }
    }
}

pub async fn handle_purge(
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) -> Result<(), MethodErr> {
    clear_all_mounts(mount_map.clone(), None)
        .await
        .map_err(to_method_err)?;
    wait_unmount_completed(mount_map, None, None, UNMOUNTER_INTERVAL * 2)
        .await
        .map_err(to_method_err)?;

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
            uninstall_shader_cache_dlc(steam_game_id, conn.clone())
                .await
                .map_err(to_method_err)?;
        }
    }
    Ok(())
}

pub async fn handle_unmount(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMap,
) -> Result<(), MethodErr> {
    let request: UnmountRequest = protobuf::Message::parse_from_bytes(&raw_bytes)
        .map_err(|e| dbus::MethodErr::invalid_arg(&e))?;
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
        shader_cache_mount
            .remove_game_from_db_list(request.steam_app_id)
            .map_err(to_method_err)?;
    } else {
        return Err(MethodErr::failed("VM had never mounted shader cache"));
    }

    Ok(())
}

pub fn process_unmount_queue(
    vm_id: &VmId,
    shader_cache_mount: &mut ShaderCacheMount,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let mut errors: Vec<String> = vec![];
    let mut to_remove: Vec<SteamAppId> = vec![];

    for &steam_app_id in shader_cache_mount.get_unmount_queue() {
        debug!("Attempting to unmount {} for {:?}", steam_app_id, vm_id);

        let unmount_result = unmount(shader_cache_mount, steam_app_id);
        let still_mounted = unmount_result.is_err();

        if let Err(e) =
            signal_mount_status(still_mounted, vm_id, steam_app_id, &unmount_result, &conn)
        {
            error!("Failed to send unmount signal, {:?}\n", e)
        }

        if still_mounted {
            errors.push(format!(
                "Failed to unmount {}, {:?}\n",
                steam_app_id,
                unmount_result.unwrap_err()
            ));
        } else {
            to_remove.push(steam_app_id);
        }
    }

    shader_cache_mount.dequeue_unmount_multi(&to_remove);

    if errors.is_empty() {
        return Ok(());
    }
    Err(anyhow!("{:?}", errors))
}

pub async fn clear_all_mounts(mount_map: ShaderCacheMountMap, vm_id: Option<VmId>) -> Result<()> {
    // Queue unmount-everything and clear queued mounts.
    // This function is called on Purge (vm_id is None) and on Borealis exit
    // (vm_id is set).
    let mut mount_map = mount_map.write().await;
    let mut failed_unmounts: HashSet<VmId> = HashSet::new();

    if let Some(vm_id) = vm_id {
        if let Some(shader_cache_mount) = mount_map.get_mut(&vm_id) {
            shader_cache_mount.clear_mount_queue();
            if let Err(e) = shader_cache_mount.reset_foz_db_list() {
                error!("Failed to queue unmount all for {:?}: {}", vm_id, e);
                failed_unmounts.insert(vm_id);
            }
        } else {
            failed_unmounts.insert(vm_id);
        }
    } else {
        for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
            shader_cache_mount.clear_mount_queue();
            if let Err(e) = shader_cache_mount.reset_foz_db_list() {
                error!("Failed to queue unmount all for {:?}: {}", vm_id, e);
                failed_unmounts.insert(vm_id.clone());
            }
        }
    }

    if failed_unmounts.is_empty() {
        return Ok(());
    }
    Err(anyhow!(
        "Failed to queue unmount for all: {:?}",
        failed_unmounts
    ))
}

pub async fn handle_vm_stopped(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMap,
) -> Result<(), MethodErr> {
    let stopping_signal: VmStoppingSignal = protobuf::Message::parse_from_bytes(&raw_bytes)
        .map_err(|e| dbus::MethodErr::invalid_arg(&e))?;
    let vm_id = VmId {
        vm_name: stopping_signal.name,
        vm_owner_id: stopping_signal.owner_id,
    };

    clear_all_mounts(mount_map, Some(vm_id))
        .await
        .map_err(to_method_err)?;

    Ok(())
}

pub async fn wait_unmount_completed(
    mount_map: ShaderCacheMountMap,
    steam_app_id: Option<SteamAppId>,
    vm_id: Option<VmId>,
    timeout: std::time::Duration,
) -> Result<()> {
    // Wait for unmount to be complete for all
    let max_wait_time = if timeout < UNMOUNTER_INTERVAL {
        debug!("Wait unmount timeout is smaller than unmounter interval, overridden to interval");
        UNMOUNTER_INTERVAL
    } else {
        timeout
    };

    let start_time = std::time::Instant::now();
    loop {
        let mut still_mounted = false;
        {
            let mount_map = mount_map.read().await;
            let mut shader_cache_mounts = std::vec::Vec::new();
            if let Some(ref vm_id) = vm_id {
                if let Some(item) = mount_map.get(vm_id) {
                    shader_cache_mounts.push(item);
                }
            } else {
                shader_cache_mounts.extend(mount_map.values());
            }

            // Note: We could use is_mounted() function instead of getting
            // mount information manually here, but is_mounted() makes `mount`
            // command every time, which we should avoid doing in this loop
            // to minimize our time holding onto the read lock.
            // TODO(b/266499555): code cleanup and restructuring
            let output = std::process::Command::new("mount")
                .stdout(std::process::Stdio::piped())
                .output()?;
            let mount_list = String::from_utf8(output.stdout)?;

            for shader_cache_mount in shader_cache_mounts {
                let path_to_check: String = if let Some(steam_app_id) = steam_app_id {
                    shader_cache_mount.get_str_absolute_mount_destination_path(steam_app_id)?
                } else {
                    let shader_cache_mount_str_path =
                        shader_cache_mount
                            .mount_base_path
                            .to_str()
                            .ok_or_else(|| anyhow!("Failed to convert PathBuf to string"))?;
                    String::from(shader_cache_mount_str_path)
                };

                if mount_list.contains(&path_to_check) {
                    still_mounted = true;
                    break;
                }
            }
        }

        if !still_mounted {
            return Ok(());
        }

        if start_time.elapsed() > max_wait_time {
            break;
        }
        // No point checking more frequently than periodic unmounter
        tokio::time::sleep(UNMOUNTER_INTERVAL).await;
    }

    Err(anyhow!("Time out while checking for mount status"))
}
