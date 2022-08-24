// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(b/249164252): (un)mounting behaviour and overall flow change needed once
// b/235392416 is implemented.

use crate::common::*;
use crate::shader_cache_mount::*;

use anyhow::{anyhow, Result};
use dbus::{channel::Sender, nonblock::SyncConnection};
use std::{
    process::{Command, Stdio},
    sync::Arc,
};
use sys_util::{debug, error};
use system_api::shadercached::ShaderCacheMountStatus;

fn is_mounted(path: &str) -> Result<bool> {
    // Determine if the directory is mounted
    let output = Command::new("mount").stdout(Stdio::piped()).output()?;
    let mount_list = String::from_utf8(output.stdout)?;
    Ok(mount_list.contains(path))
}

pub fn unmount(shader_cache_mount: &ShaderCacheMount) -> Result<()> {
    // Unmount the shader cache dlc
    let path = shader_cache_mount.get_str_absolute_mount_destination_path()?;

    if !is_mounted(path)? {
        debug!("Path {} is already unmounted, skipping", path);
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

    Ok(())
}

pub fn bind_mount(shader_cache_mount: &ShaderCacheMount) -> Result<()> {
    // Mount the shader cache DLC for the requested Steam application ID
    let src = shader_cache_mount.dlc_content_path()?;
    let dst = shader_cache_mount.get_str_absolute_mount_destination_path()?;

    debug!("Mounting {} into {}", src, dst);

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
    conn: &Arc<SyncConnection>,
    mount_status: &ShaderCacheMountStatus,
) -> Result<()> {
    // Tell Cicerone shader cache has been (un)mounted, so that it can continue
    // process calls.
    let mounted_signal = dbus::Message::new_signal(PATH_NAME, INTERFACE_NAME, MOUNT_SIGNAL_NAME)
        .map_err(|e| anyhow!("Failed to create signal: {}", e))?
        .append1(
            protobuf::Message::write_to_bytes(mount_status)
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
            let mount_result =
                unmount(shader_cache_mount).and_then(|_| bind_mount(shader_cache_mount));

            shader_cache_mount.mounted = mount_result.is_ok();

            let mut mount_status = ShaderCacheMountStatus::new();
            mount_status.set_mounted(mount_result.is_ok());
            mount_status.set_vm_name(vm_id.vm_name.clone());
            mount_status.set_vm_owner_id(vm_id.vm_owner_id.clone());
            mount_status.set_steam_app_id(steam_app_id_to_mount);
            if let Err(e) = mount_result {
                mount_status.set_error(e.to_string());
                errors.push(format!("Failed to mount, {:?}\n", e));
            }

            if let Err(e) = signal_mount_status(&conn, &mount_status) {
                errors.push(format!("Failed to send mount signal, {:?}\n", e));
            }
        }
    }

    if errors.is_empty() {
        Ok(())
    } else {
        Err(anyhow!("Mount failure: {:?}", errors))
    }
}

pub async fn unmount_dlc(
    steam_app_id_to_unmount: SteamAppId,
    mount_map: ShaderCacheMountMap,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    // Iterate through all mount points then attempt to unmount shader cache if
    // |target_steam_app_id| matches |steam_app_id_to_unmount|
    let mut mount_map = mount_map.write().await;
    let mut errors: Vec<String> = vec![];

    mount_map.retain(|vm_id, shader_cache_mount| {
        if shader_cache_mount.target_steam_app_id == steam_app_id_to_unmount
            && shader_cache_mount.mounted
        {
            debug!("Unmounting: {:?}", vm_id);
            let unmount_result = unmount(shader_cache_mount);
            // Retain if unmount failed, delete the entry otherwise
            let retain = unmount_result.is_err();

            let mut mount_status = ShaderCacheMountStatus::new();
            mount_status.set_mounted(unmount_result.is_err());
            mount_status.set_vm_name(vm_id.vm_name.clone());
            mount_status.set_vm_owner_id(vm_id.vm_owner_id.clone());
            mount_status.set_steam_app_id(steam_app_id_to_unmount);
            if let Err(e) = unmount_result {
                mount_status.set_error(e.to_string());
                errors.push(format!("Failed to unmount, {:?}\n", e));
            } else {
                debug!("Deleting mount point: {:?}", vm_id);
            }

            if let Err(e) = signal_mount_status(&conn, &mount_status) {
                errors.push(format!("Failed to send unmount signal, {:?}\n", e));
            }

            return retain;
        }

        // Retain entries that should not be unmounted
        true
    });

    if errors.is_empty() {
        Ok(())
    } else {
        Err(anyhow!("Unmount failure: {:?}", errors))
    }
}

pub async fn unmount_all(mount_map: ShaderCacheMountMap) {
    let mut mount_map = mount_map.write().await;

    for (cache_id, cache_metadata) in mount_map.iter_mut() {
        if cache_metadata.mounted {
            debug!("Unmounting: {:?}", cache_id);
            if let Err(e) = unmount(cache_metadata) {
                error!("Failed to unmount {:?}: {}", cache_id, e);
            }
        }
    }
}
