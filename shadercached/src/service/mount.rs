// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;
use crate::shader_cache_mount::*;

use anyhow::{anyhow, Result};
use dbus::{channel::Sender, nonblock::SyncConnection};
use libchromeos::sys::{debug, info};
use std::path::Path;
use std::{
    fs,
    process::{Command, Stdio},
    sync::Arc,
};
use system_api::shadercached::ShaderCacheMountStatus;

fn is_mounted(path: &str) -> Result<bool> {
    // Determine if the directory is mounted
    let output = Command::new("mount").stdout(Stdio::piped()).output()?;
    let mount_list = String::from_utf8(output.stdout)?;
    Ok(mount_list.contains(path))
}

pub fn unmount(shader_cache_mount: &ShaderCacheMount, steam_app_id: SteamAppId) -> Result<()> {
    // Unmount the shader cache dlc
    let path = shader_cache_mount.get_str_absolute_mount_destination_path(steam_app_id)?;
    if !is_mounted(&path)? {
        debug!(
            "Path {} is already unmounted or does not exist, skipping",
            path
        );
        return Ok(());
    }

    debug!("Unmounting {}", path);

    let mut unmount_cmd = Command::new("umount").arg(&path).spawn()?;
    let exit_status = unmount_cmd.wait()?;

    if !exit_status.success() {
        return Err(anyhow!(
            "Unmount failed with code {}",
            exit_status.code().unwrap_or(-1)
        ));
    }

    fs::remove_dir(&path)?;
    Ok(())
}

pub fn bind_mount(shader_cache_mount: &ShaderCacheMount, steam_app_id: SteamAppId) -> Result<()> {
    // Mount the shader cache DLC for the requested Steam application ID
    let src = shader_cache_mount.dlc_content_path()?;
    // destination path has been created at handle_install, which may involve
    // requesting permissions from concierge
    let dst = shader_cache_mount.get_str_absolute_mount_destination_path(steam_app_id)?;

    if is_mounted(&dst)? {
        // If directory is mounted, assume it is correctly mounted because the
        // directory permission is 750 - ie. only shadercached can modify it and
        // assume shadercached mounts correct path.
        //
        // This allows `bind_mount` to return success if directory is already
        // mounted without re-mounting the directory.
        debug!("Path {} is already mounted, skipping", dst);
        return Ok(());
    }

    info!("Mounting {} into {}", src, dst);

    let mut mount_cmd = Command::new("mount")
        .arg("--bind")
        .arg(src)
        .arg(dst)
        .spawn()?;
    let exit_status = mount_cmd.wait()?;

    if !exit_status.success() {
        return Err(anyhow!(
            "Mount failed with code {}",
            exit_status.code().unwrap_or(-1)
        ));
    }

    Ok(())
}

pub fn signal_mount_status(
    is_mounted: bool,
    vm_id: &VmId,
    steam_app_id: SteamAppId,
    mount_op_result: &Result<()>,
    conn: &Arc<SyncConnection>,
) -> Result<()> {
    let mut mount_status = ShaderCacheMountStatus::new();
    mount_status.set_mounted(is_mounted);
    mount_status.set_vm_name(vm_id.vm_name.clone());
    mount_status.set_vm_owner_id(vm_id.vm_owner_id.clone());
    mount_status.set_steam_app_id(steam_app_id);

    if let Err(e) = mount_op_result {
        mount_status.set_error(e.to_string());
    }

    // Tell Cicerone shader cache has been (un)mounted, so that it can continue
    // process calls.
    let mounted_signal = dbus::Message::new_signal(PATH_NAME, INTERFACE_NAME, MOUNT_SIGNAL_NAME)
        .map_err(|e| anyhow!("Failed to create signal: {}", e))?
        .append1(
            protobuf::Message::write_to_bytes(&mount_status)
                .map_err(|e| anyhow!("Failed to parse protobuf: {}", e))?,
        );
    debug!("Sending mount status signal.. {:?}", mount_status);
    conn.send(mounted_signal)
        .map_err(|_| anyhow!("Failed to send signal"))?;
    Ok(())
}

pub async fn mount_dlc(
    steam_app_id_to_mount: SteamAppId,
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    // Iterate through all mount points then attempt to mount shader cache if
    // |target_steam_app_id| matches |steam_app_id_to_mount| (which was just
    // installed)
    let mut mount_map = mount_map.write().await;
    let mut errors: Vec<String> = vec![];

    for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
        if shader_cache_mount.target_steam_app_id == steam_app_id_to_mount
            && !shader_cache_mount.mounted
        {
            debug!("Mounting: {:?}", vm_id);
            let mount_result = shader_cache_mount
                .setup_mount_destination(vm_id, steam_app_id_to_mount, conn.clone())
                .await
                .and_then(|_| bind_mount(shader_cache_mount, steam_app_id_to_mount))
                .and_then(|_| shader_cache_mount.add_game_to_db_list(steam_app_id_to_mount));
            shader_cache_mount.mounted = mount_result.is_ok();

            if let Err(e) = signal_mount_status(
                mount_result.is_ok(),
                vm_id,
                steam_app_id_to_mount,
                &mount_result,
                &conn,
            ) {
                errors.push(format!("Failed to send mount signal, {:?}\n", e));
            }

            if let Err(ref e) = mount_result {
                errors.push(format!("Failed to mount {:?}, {:?}", vm_id, e));
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
    mount_map: ShaderCacheMountMap,
) -> Result<()> {
    // Iterate through all mount points then queue unmount for
    // |steam_app_id_to_unmount|
    let mut errors: Vec<String> = vec![];

    {
        // |mount_map| with write mutex needs to go out of scope after this
        // loop so that background unmounter can take the mutex
        let mut mount_map = mount_map.write().await;
        for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
            if shader_cache_mount.target_steam_app_id == steam_app_id_to_unmount {
                shader_cache_mount.target_steam_app_id = 0;
            }
            let found = shader_cache_mount.remove_game_from_db_list(steam_app_id_to_unmount)?;
            if found {
                debug!(
                    "Queuing unmount {} for {:?}",
                    steam_app_id_to_unmount, vm_id
                );
                if let Err(e) = shader_cache_mount.queue_unmount(steam_app_id_to_unmount) {
                    errors.push(format!("Failed to queue unmount: {:?}\n", e));
                }
            }
        }
    }

    // Wait for unmount to be complete for all
    let mut success = false;
    for _ in 0..10 {
        tokio::time::sleep(UNMOUNTER_INTERVAL).await;
        let mut still_mounted = false;
        {
            // |mount_map| read mutex not required for background unmounter, but
            // given |unmount_dlc| depends on other threads working on
            // |mount_map|, it is safer to be conservative with mutex lifetimes.
            let mount_map = mount_map.read().await;
            for (vm_id, shader_cache_mount) in mount_map.iter() {
                let str_path = shader_cache_mount
                    .get_str_absolute_mount_destination_path(steam_app_id_to_unmount)?;
                info!("checking if {} exists..", str_path);
                let path = Path::new(&str_path);
                if path.exists() {
                    debug!("{:?} has {} still mounted", vm_id, steam_app_id_to_unmount);
                    still_mounted = true;
                    break;
                }
            }
        }
        if !still_mounted {
            info!("{} has been unmounted for all VMs", steam_app_id_to_unmount);
            success = true;
            break;
        }
    }

    if !success {
        errors.push("DLC contents still mounted".into());
    }

    if errors.is_empty() {
        Ok(())
    } else {
        Err(anyhow!("Unmount failure: {:?}", errors))
    }
}
