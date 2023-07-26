// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Mount and related operations are aggregated here because:
// 1. Mounting is a privileged operation that requires auditing
// 2. The plan is to move shader cache location to shadercached's cryptohome
//    entirely. Along with non-DLC distributed precompiled cache, we won't need
//    any mounting or assisting operations.
// Contact endlesspring@ for more details.

use super::{ShaderCacheMount, ShaderCacheMountMap};
use crate::common::*;

use anyhow::{anyhow, Result};
use log::debug;
use std::fs;

pub mod helpers {
    #[cfg(test)]
    use mockall::automock;
    #[cfg_attr(test, automock)]
    pub mod privileged_ops {
        #![allow(dead_code)]

        use anyhow::{anyhow, Result};
        use log::debug;
        use std::process::{Command, Stdio};

        pub fn get_mount_list() -> Result<String> {
            let output = Command::new("mount").stdout(Stdio::piped()).output()?;
            let mount_list = String::from_utf8(output.stdout)?;

            Ok(mount_list)
        }

        pub fn bind_mount(src: &str, dst: &str) -> Result<()> {
            debug!("Mounting {} into {}", src, dst);
            let exit_status = Command::new("mount")
                .arg("--bind")
                .arg(src)
                .arg(dst)
                .spawn()?
                .wait()?;
            if exit_status.success() {
                return Ok(());
            }
            Err(anyhow!(
                "Process failed with code {}",
                exit_status.code().unwrap_or(-1)
            ))
        }

        pub fn unmount(path: &str) -> Result<()> {
            debug!("Unmounting {}", path);
            let exit_status = Command::new("umount").arg(path).spawn()?.wait()?;
            if exit_status.success() {
                return Ok(());
            }
            Err(anyhow!(
                "Process failed with code {}",
                exit_status.code().unwrap_or(-1)
            ))
        }
    }
}

#[cfg(test)]
use helpers::mock_privileged_ops as privileged_ops;
#[cfg(not(test))]
use helpers::privileged_ops;

impl ShaderCacheMount {
    pub(super) fn unmount(self: &ShaderCacheMount, steam_app_id: SteamAppId) -> Result<()> {
        // Unmount the shader cache dlc
        let path = self.get_str_absolute_mount_destination_path(steam_app_id)?;
        if !self.is_game_mounted(steam_app_id, None)? {
            debug!(
                "Path {} is already unmounted or does not exist, skipping",
                path
            );
            return Ok(());
        }

        privileged_ops::unmount(&path)?;
        fs::remove_dir(&path)?;
        Ok(())
    }

    pub fn bind_mount_dlc(self: &ShaderCacheMount, steam_app_id: SteamAppId) -> Result<()> {
        debug!("Bind mounting dlc for {}", steam_app_id);
        // Mount the shader cache DLC for the requested Steam application ID
        let src = self.dlc_content_path(steam_app_id)?;
        // destination path has been created at handle_install, which may involve
        // requesting permissions from concierge
        let dst = self.get_str_absolute_mount_destination_path(steam_app_id)?;

        if self.is_game_mounted(steam_app_id, None)? {
            // If directory is mounted, assume it is correctly mounted because the
            // directory permission is 750 - ie. only shadercached can modify it and
            // assume shadercached mounts correct path.
            //
            // This allows `bind_mount` to return success if directory is already
            // mounted without re-mounting the directory.
            debug!("Path {} is already mounted, skipping", dst);
            return Ok(());
        };

        privileged_ops::bind_mount(&src, &dst)
    }

    pub fn local_precompiled_cache_path(&self) -> Result<String> {
        self.precompiled_cache_path
            .clone()
            .into_os_string()
            .into_string()
            .map_err(|e| anyhow!("Failed to convert path to string: {:?}", e))
    }

    fn dlc_content_path(&self, steam_app_id: SteamAppId) -> Result<String> {
        // Generate DLC content
        let path = IMAGE_LOADER
            .join(steam_app_id_to_dlc(steam_app_id))
            .join("package/root")
            .join(self.get_relative_mesa_cache_path()?);

        if !path.exists() {
            return Err(anyhow!(
                "No shader cache DLC for Steam app {}, expected path {:?}",
                steam_app_id,
                path.as_os_str()
            ));
        }

        path.into_os_string()
            .into_string()
            .map_err(|os_str| anyhow!("Failed to convert path to string: {:?}", os_str))
    }

    pub fn is_game_mounted(
        &self,
        steam_app_id: SteamAppId,
        mount_list: Option<&str>,
    ) -> Result<bool> {
        debug!("Checking if {} is mounted", steam_app_id);
        if let Some(mount_list) = mount_list {
            return Ok(
                mount_list.contains(&self.get_str_absolute_mount_destination_path(steam_app_id)?)
            );
        }
        Ok(privileged_ops::get_mount_list()?
            .contains(&self.get_str_absolute_mount_destination_path(steam_app_id)?))
    }

    fn is_any_mounted(&self, mount_list: &str) -> Result<bool> {
        let base_path = self
            .get_mount_base_path()?
            .to_str()
            .ok_or_else(|| anyhow!("Failed to convert PathBuf to string"))?;

        Ok(mount_list.contains(base_path))
    }
}

impl ShaderCacheMountMap {
    pub async fn wait_unmount_completed(
        self: &ShaderCacheMountMap,
        steam_app_id: Option<SteamAppId>,
        timeout: std::time::Duration,
    ) -> Result<()> {
        // Wait for unmount to be complete for all
        let max_wait_time = if timeout < UNMOUNTER_INTERVAL {
            debug!(
                "Wait unmount timeout is smaller than unmounter interval, overridden to interval"
            );
            UNMOUNTER_INTERVAL
        } else {
            timeout
        };

        let start_time = std::time::Instant::now();
        loop {
            let mut still_mounted = false;
            {
                let mount_map = self.read().await;
                let mount_list = privileged_ops::get_mount_list()?;
                for shader_cache_mount in mount_map.values() {
                    still_mounted = if let Some(steam_app_id) = steam_app_id {
                        shader_cache_mount.is_game_mounted(steam_app_id, Some(&mount_list))?
                    } else {
                        shader_cache_mount.is_any_mounted(&mount_list)?
                    };
                    if still_mounted {
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
}
