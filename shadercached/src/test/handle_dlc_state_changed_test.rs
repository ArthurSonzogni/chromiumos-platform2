// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::sync::Arc;

use anyhow::Result;
use libchromeos::sys::debug;
use serial_test::serial;
use system_api::dlcservice::dlc_state::State;
use system_api::dlcservice::DlcState;

use crate::common::{steam_app_id_to_dlc, SteamAppId};
use crate::dbus_wrapper::MockDbusConnectionTrait;
use crate::dlc_queue::new_queue;
use crate::service::handle_dlc_state_changed;
use crate::shader_cache_mount::{mount_ops, new_mount_map, VmId};
use crate::test::common::{
    add_shader_cache_mount, enqueue_mount, foz_db_list_contains, mock_gpucache,
    mock_shader_cache_dlc, mount_destination_exists,
};

fn mock_dlc_state(steam_app_id: SteamAppId, state: State) -> Result<Vec<u8>> {
    let mut dlc_state = DlcState::new();
    dlc_state.id = steam_app_id_to_dlc(steam_app_id);
    dlc_state.state = state.into();
    Ok(protobuf::Message::write_to_bytes(&dlc_state)?)
}

fn mock_dbus_conn(send_calls: usize) -> Arc<MockDbusConnectionTrait> {
    let mut mock_conn = MockDbusConnectionTrait::new();
    if send_calls > 0 {
        mock_conn
            .expect_send()
            .times(send_calls)
            .returning(|_| -> Result<u32, ()> { Ok(0) });
    }
    Arc::new(mock_conn)
}

#[tokio::test]
async fn dlc_installed_no_mount_queue() -> Result<()> {
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(0);
    assert!(dlc_queue.write().await.add_installing(&42));

    assert!(dlc_queue.read().await.get_installing_set().contains(&42));
    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 1);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);

    handle_dlc_state_changed(
        mock_dlc_state(42, State::INSTALLED)?,
        mount_map.clone(),
        dlc_queue.clone(),
        dbus_conn,
    )
    .await;

    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 0);
    Ok(())
}

#[tokio::test]
async fn dlc_installing_no_mount_queue() -> Result<()> {
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(0);
    assert!(dlc_queue.write().await.add_installing(&42));

    assert!(dlc_queue.read().await.get_installing_set().contains(&42));
    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 1);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);

    handle_dlc_state_changed(
        mock_dlc_state(42, State::INSTALLING)?,
        mount_map.clone(),
        dlc_queue.clone(),
        dbus_conn,
    )
    .await;

    let dlc_queue_read = dlc_queue.read().await;
    assert!(dlc_queue_read.get_installing_set().contains(&42));
    assert_eq!(dlc_queue_read.get_installing_set().len(), 1);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);
    Ok(())
}

#[tokio::test]
async fn dlc_install_failed() -> Result<()> {
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(0);
    assert!(dlc_queue.write().await.add_installing(&42));

    assert!(dlc_queue.read().await.get_installing_set().contains(&42));
    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 1);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);

    handle_dlc_state_changed(
        mock_dlc_state(42, State::NOT_INSTALLED)?,
        mount_map.clone(),
        dlc_queue.clone(),
        dbus_conn,
    )
    .await;

    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 0);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);
    Ok(())
}

#[tokio::test]
#[serial(mount_list, bind_mount)]
async fn dlc_installed_mount() -> Result<()> {
    let game_id = mock_shader_cache_dlc()?;

    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(1);
    assert!(dlc_queue.write().await.add_installing(&game_id));

    assert!(dlc_queue
        .read()
        .await
        .get_installing_set()
        .contains(&game_id));
    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 1);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);

    let vm_id = VmId::new("vm", "owner");
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;

    enqueue_mount(mount_map.clone(), &vm_id, game_id).await?;

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    let bind_mount_context = mount_ops::helpers::mock_privileged_ops::bind_mount_context();
    let mock_gpu_cache_path = mock_gpu_cache.path().to_path_buf();
    bind_mount_context.expect().return_once(move |src, dst| {
        debug!("Bind mount called with {} {}", src, dst);
        let src_path = Path::new(src);
        let dst_path = Path::new(dst);
        assert!(dst_path.exists());
        assert!(
            dst_path.starts_with(mock_gpu_cache_path.join("render_server/mesa_shader_cache_sf"))
        );
        assert!(dst_path.ends_with(game_id.to_string()));
        assert!(src_path.exists());
        Ok(())
    });

    handle_dlc_state_changed(
        mock_dlc_state(game_id, State::INSTALLED)?,
        mount_map.clone(),
        dlc_queue.clone(),
        dbus_conn,
    )
    .await;

    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 0);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);

    assert!(foz_db_list_contains(&mock_gpu_cache, game_id)?);
    assert!(mount_destination_exists(&mock_gpu_cache, game_id).await?);

    Ok(())
}

#[tokio::test]
async fn dlc_install_failed_mount_queue() -> Result<()> {
    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(1);
    assert!(dlc_queue.write().await.add_installing(&42));

    assert!(dlc_queue.read().await.get_installing_set().contains(&42));
    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 1);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);

    let vm_id = VmId::new("vm", "owner");
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;

    enqueue_mount(mount_map.clone(), &vm_id, 42).await?;

    handle_dlc_state_changed(
        mock_dlc_state(42, State::NOT_INSTALLED)?,
        mount_map.clone(),
        dlc_queue.clone(),
        dbus_conn,
    )
    .await;

    assert_eq!(dlc_queue.read().await.get_installing_set().len(), 0);
    assert_eq!(dlc_queue.read().await.get_install_queue().len(), 0);
    assert_eq!(dlc_queue.read().await.get_uninstall_queue().len(), 0);

    let mount_map_read = mount_map.write().await;
    let shader_cache_mount = mount_map_read.get(&vm_id).unwrap();
    assert_eq!(shader_cache_mount.get_mount_queue().len(), 0);
    drop(mount_map_read);

    Ok(())
}
