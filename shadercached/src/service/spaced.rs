// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    collections::HashSet,
    path::{Path, PathBuf},
    sync::Arc,
};

use super::helper::unsafe_quota::{set_quota_limited, set_quota_normal};
use crate::{
    common::{CRYPTO_HOME, PRECOMPILED_CACHE_DIR},
    shader_cache_mount::ShaderCacheMountMapPtr,
};
use dbus::nonblock::SyncConnection;
use libchromeos::sys::{debug, info, warn};

use anyhow::Result;
use system_api::spaced::{StatefulDiskSpaceState, StatefulDiskSpaceUpdate};
use tokio::sync::Mutex;

lazy_static! {
    static ref PURGED: Mutex<bool> = Mutex::new(false);
    static ref LIMITED_QUOTA_PATHS: Mutex<HashSet<PathBuf>> = Mutex::new(HashSet::new());
}

fn delete_all_files(path: &Path) -> Result<()> {
    debug!("Cleaning up {}", path.display());
    for dir_entry in (std::fs::read_dir(path)?).flatten() {
        if dir_entry.path().is_dir() {
            std::fs::remove_dir_all(dir_entry.path())?;
        } else {
            std::fs::remove_file(dir_entry.path())?;
        }
    }
    Ok(())
}

fn get_all_precompiled_cache_dir() -> Result<Vec<PathBuf>> {
    let mut dirs: Vec<PathBuf> = vec![];
    for dir_entry in (std::fs::read_dir(CRYPTO_HOME)?).flatten() {
        let user_cryptohome = dir_entry.path();
        dirs.push(user_cryptohome.join(PRECOMPILED_CACHE_DIR))
    }
    Ok(dirs)
}

pub async fn delete_precompiled_cache(mount_map: ShaderCacheMountMapPtr) -> Result<()> {
    // TODO(b/271776528): utilize ShaderCacheMount once it is reliable
    // SoT for cryptohome and mounts. For now, just get the lock and
    // call get_all_precompiled_cache_dir(). This has no runtime
    // differences.
    let _mount_map = mount_map.write().await;
    for local_cache_dir in get_all_precompiled_cache_dir()? {
        for dir_entry in (std::fs::read_dir(local_cache_dir)?).flatten() {
            info!("Deleting all files at {}", dir_entry.path().display());
            delete_all_files(&dir_entry.path())?;
        }
    }
    Ok(())
}

pub async fn handle_disk_space_update(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
    conn: Arc<SyncConnection>,
) -> Result<()> {
    let update_signal: StatefulDiskSpaceUpdate = protobuf::Message::parse_from_bytes(&raw_bytes)
        .map_err(|e| dbus::MethodErr::invalid_arg(&e))?;

    debug!(
        "Spaced status {:?}, free space bytes {}",
        update_signal.get_state(),
        update_signal.free_space_bytes
    );

    let mut is_purged = PURGED.lock().await;
    let mut limited_quota_paths = LIMITED_QUOTA_PATHS.lock().await;
    // Clean things up if low
    // LOW = < 1%
    if update_signal.get_state() == StatefulDiskSpaceState::LOW
        || update_signal.get_state() == StatefulDiskSpaceState::CRITICAL
    {
        if !*is_purged {
            // In the first attempt, just delete the DLCs and see if disk space
            // recovers.
            info!("Low/critical disk space, removing all shader cache DLCs");
            // Set is_purged  early, so that we do the next step if DLC
            // uninstallation fails.
            *is_purged = true;
            // Purge all shader cache DLCs, only once until recovery
            super::unmount_and_uninstall_all_shader_cache_dlcs(mount_map.clone(), conn.clone())
                .await?;
        } else {
            // spaced will continue sending signals if the disk stays near full.
            // Clean up downloaded shader cache contents.
            delete_precompiled_cache(mount_map.clone()).await?;

            // TODO(b/271776528): Ditto as above
            let _mount_map = mount_map.write().await;
            for local_cache_dir in get_all_precompiled_cache_dir()? {
                if !limited_quota_paths.contains(&local_cache_dir) {
                    if let Err(e) = set_quota_limited(&local_cache_dir) {
                        warn!(
                            "Failed to limit quota at {}: {}",
                            local_cache_dir.display(),
                            e
                        );
                    } else {
                        limited_quota_paths.insert(local_cache_dir);
                    }
                }
            }
        }
    } else if update_signal.get_state() == StatefulDiskSpaceState::NORMAL {
        debug!("Normal disk space, recovering if required");
        // TODO(b/271776528): Ditto as above
        let _mount_map = mount_map.write().await;

        if *is_purged {
            *is_purged = false;
        }
        let mut failed = HashSet::new();
        for local_cache_dir in limited_quota_paths.drain() {
            if let Err(e) = set_quota_normal(&local_cache_dir) {
                warn!(
                    "Failed to limit quota at {}: {}",
                    local_cache_dir.display(),
                    e
                );
                failed.insert(local_cache_dir);
            }
        }
        limited_quota_paths.extend(failed);
    }

    Ok(())
}
