// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::sync::Arc;

use anyhow::Result;
use serial_test::serial;
use system_api::spaced::{StatefulDiskSpaceState, StatefulDiskSpaceUpdate};

use crate::common::{steam_app_id_to_dlc, SteamAppId, CRYPTO_HOME, PRECOMPILED_CACHE_DIR};
use crate::dbus_wrapper::{dbus_constants, MockDbusConnectionTrait};
use crate::service::handle_disk_space_update;
use crate::service::helper::unsafe_ops::mock_quota;
use crate::service::spaced::{LIMITED_QUOTA_PATHS, PURGED};
use crate::shader_cache_mount::{mount_ops, new_mount_map, VmId};
use crate::test::common::{
    add_shader_cache_mount, enqueue_mount, foz_db_list_empty, mock_gpucache, mock_shader_cache_dlc,
    populate_precompiled_cache, simulate_mounted,
};

fn mock_disk_space_update(state: StatefulDiskSpaceState) -> Result<Vec<u8>> {
    let mut disk_space_update = StatefulDiskSpaceUpdate::new();
    disk_space_update.state = state.into();
    // free_space_bytes is not used by shadercached so it does not matter
    disk_space_update.free_space_bytes = 42;
    Ok(protobuf::Message::write_to_bytes(&disk_space_update)?)
}

fn mock_dbus_conn(
    games_to_uninstall: &[SteamAppId],
    get_installed_calls: usize,
) -> Arc<MockDbusConnectionTrait> {
    let mut mock_conn = MockDbusConnectionTrait::new();

    let mut dlc_ids_to_uninstall: HashSet<String> = games_to_uninstall
        .iter()
        .map(|steam_app_id| steam_app_id_to_dlc(*steam_app_id))
        .collect();
    mock_conn
        .expect_call_dbus_method()
        .times(games_to_uninstall.len())
        .returning(move |_, _, _, method, (dlc_id,): (String,)| {
            if method == dbus_constants::dlc_service::UNINSTALL_METHOD {
                assert!(dlc_ids_to_uninstall.remove(&dlc_id));
                return Box::pin(async { Ok(()) });
            }

            unreachable!();
        });

    let valid_dlc_ids: Vec<String> = games_to_uninstall
        .iter()
        .map(|steam_app_id| steam_app_id_to_dlc(*steam_app_id))
        .collect();
    mock_conn
        .expect_call_dbus_method()
        .times(get_installed_calls)
        .returning(move |_, _, _, method, ()| {
            let dlc_ids = valid_dlc_ids.clone();
            if method == dbus_constants::dlc_service::GET_INSTALLED_METHOD {
                return Box::pin(async move { Ok((dlc_ids,)) });
            }
            unreachable!();
        });

    Arc::new(mock_conn)
}

async fn reset_static_vars() {
    let mut is_purged = PURGED.lock().await;
    let mut paths = LIMITED_QUOTA_PATHS.lock().await;
    *is_purged = false;
    paths.drain();
}

fn precompiled_cache_empty_for_vm(vm_id: &VmId) -> Result<bool> {
    let encoded_vm_name = base64::encode_config(&vm_id.vm_name, base64::URL_SAFE);
    let base_path = CRYPTO_HOME
        .join(&vm_id.vm_owner_id)
        .join(PRECOMPILED_CACHE_DIR)
        .join(encoded_vm_name);
    let read_result = std::fs::read_dir(base_path)?;
    Ok(read_result.count() == 0)
}

#[tokio::test]
#[serial]
async fn state_normal_no_updates() -> Result<()> {
    reset_static_vars().await;
    let mount_map = new_mount_map();
    let dbus_conn = mock_dbus_conn(&[], 0);

    handle_disk_space_update(
        mock_disk_space_update(StatefulDiskSpaceState::NORMAL)?,
        mount_map.clone(),
        dbus_conn.clone(),
    )
    .await?;

    assert_eq!((*LIMITED_QUOTA_PATHS).lock().await.len(), 0);

    handle_disk_space_update(
        mock_disk_space_update(StatefulDiskSpaceState::NORMAL)?,
        mount_map.clone(),
        dbus_conn.clone(),
    )
    .await?;

    assert_eq!((*LIMITED_QUOTA_PATHS).lock().await.len(), 0);
    Ok(())
}

#[tokio::test]
#[serial]
async fn state_low_cleanup_check() -> Result<()> {
    reset_static_vars().await;
    let game_with_dlc1 = mock_shader_cache_dlc()?;
    let game_with_dlc2 = mock_shader_cache_dlc()?;

    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dbus_conn = mock_dbus_conn(&[game_with_dlc1, game_with_dlc2], 1);
    let vm_id = VmId::new("vm", "owner");

    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;
    enqueue_mount(mount_map.clone(), &vm_id, 1337).await?;
    simulate_mounted(&mock_gpu_cache, game_with_dlc1).await?;
    populate_precompiled_cache(&[&vm_id])?;

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    handle_disk_space_update(
        mock_disk_space_update(StatefulDiskSpaceState::LOW)?,
        mount_map.clone(),
        dbus_conn.clone(),
    )
    .await?;

    assert_eq!((*LIMITED_QUOTA_PATHS).lock().await.len(), 0);
    assert!(foz_db_list_empty(&mock_gpu_cache)?);

    let set_quota_limited_context = mock_quota::set_quota_limited_context();
    set_quota_limited_context.expect().return_once(move |p| {
        assert_eq!(p, &(*CRYPTO_HOME).join("owner/precompiled_cache"));
        Ok(())
    });

    handle_disk_space_update(
        mock_disk_space_update(StatefulDiskSpaceState::LOW)?,
        mount_map.clone(),
        dbus_conn.clone(),
    )
    .await?;

    let limited_quota_paths = (*LIMITED_QUOTA_PATHS).lock().await;
    assert!(limited_quota_paths.contains(&(*CRYPTO_HOME).join("owner/precompiled_cache")));
    assert!(precompiled_cache_empty_for_vm(&vm_id)?);

    Ok(())
}

#[tokio::test]
#[serial]
async fn state_low_then_normal() -> Result<()> {
    reset_static_vars().await;
    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dbus_conn = mock_dbus_conn(&[], 1);
    let vm_id = VmId::new("vm", "owner");

    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;
    enqueue_mount(mount_map.clone(), &vm_id, 1337).await?;
    populate_precompiled_cache(&[&vm_id])?;

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    handle_disk_space_update(
        mock_disk_space_update(StatefulDiskSpaceState::LOW)?,
        mount_map.clone(),
        dbus_conn.clone(),
    )
    .await?;

    assert_eq!((*LIMITED_QUOTA_PATHS).lock().await.len(), 0);
    assert!(foz_db_list_empty(&mock_gpu_cache)?);

    let set_quota_limited_context = mock_quota::set_quota_limited_context();
    set_quota_limited_context.expect().return_once(move |p| {
        assert_eq!(p, &(*CRYPTO_HOME).join("owner/precompiled_cache"));
        Ok(())
    });

    handle_disk_space_update(
        mock_disk_space_update(StatefulDiskSpaceState::LOW)?,
        mount_map.clone(),
        dbus_conn.clone(),
    )
    .await?;

    let limited_quota_paths = (*LIMITED_QUOTA_PATHS).lock().await;
    assert!(limited_quota_paths.contains(&(*CRYPTO_HOME).join("owner/precompiled_cache")));
    drop(limited_quota_paths);
    assert!(precompiled_cache_empty_for_vm(&vm_id)?);

    let set_quota_normal_context = mock_quota::set_quota_normal_context();
    set_quota_normal_context.expect().return_once(move |p| {
        assert_eq!(p, &(*CRYPTO_HOME).join("owner/precompiled_cache"));
        Ok(())
    });

    handle_disk_space_update(
        mock_disk_space_update(StatefulDiskSpaceState::NORMAL)?,
        mount_map.clone(),
        dbus_conn.clone(),
    )
    .await?;

    assert_eq!((*LIMITED_QUOTA_PATHS).lock().await.len(), 0);

    Ok(())
}
