// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use libchromeos::sys::debug;
use serial_test::serial;
use std::path::Path;
use std::sync::Arc;
use tempfile::TempDir;

use system_api::{concierge_service::GetVmGpuCachePathResponse, shadercached::InstallRequest};

use crate::common::SteamAppId;
use crate::shader_cache_mount::{mount_ops, VmId};
use crate::test::common::{
    clean_up_mock_shader_cache_dlc, foz_db_list_contains, get_mount_queue, mock_gpucache,
    mount_destination_exists,
};
use crate::{
    dbus_wrapper::MockDbusConnectionTrait, dlc_queue::new_queue, service::handle_install,
    shader_cache_mount::new_mount_map,
};

use super::common::mock_shader_cache_dlc;

fn mock_concierge_connection(
    mock_gpu_cache: &TempDir,
    vm_gpu_cache_path_calls: usize,
    send_calls: usize,
) -> Arc<MockDbusConnectionTrait> {
    let mut mock_conn = MockDbusConnectionTrait::new();
    if vm_gpu_cache_path_calls > 0 {
        let mock_gpu_cache_str = mock_gpu_cache.path().display().to_string();
        mock_conn
            .expect_call_dbus_method()
            .times(vm_gpu_cache_path_calls)
            .returning(move |_, _, _, _, _: (Vec<u8>,)| {
                let mut mock_response = GetVmGpuCachePathResponse::new();
                mock_response.path = mock_gpu_cache_str.clone();
                let mock_response_bytes = protobuf::Message::write_to_bytes(&mock_response)
                    .expect("Failed to parse bytes");
                Box::pin(async { Ok((mock_response_bytes,)) })
            });
    }

    if send_calls > 0 {
        mock_conn
            .expect_send()
            .times(send_calls)
            .returning(|_| -> Result<u32, ()> { Ok(0) });
    }
    Arc::new(mock_conn)
}

fn mock_install_request(
    vm_name: &str,
    vm_owner_id: &str,
    mount: bool,
    game_id: SteamAppId,
) -> Result<Vec<u8>> {
    let mut install_request = InstallRequest::new();
    install_request.vm_name = vm_name.to_string();
    install_request.vm_owner_id = vm_owner_id.to_string();
    install_request.mount = mount;
    install_request.steam_app_id = game_id;
    Ok(protobuf::Message::write_to_bytes(&install_request)?)
}

#[tokio::test]
#[serial]
async fn install_mount_single() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    handle_install(
        mock_install_request("vm", "owner", true, 1337)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn,
    )
    .await?;

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 1);
    assert!(install_queue.contains(&1337));

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 1);
    assert!(mount_queue.contains(&1337));

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_mount_multiple_vm_and_owners() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 3, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(3)
        .returning(|| Ok("".to_string()));

    handle_install(
        mock_install_request("vm", "owner", true, 42)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    handle_install(
        mock_install_request("vm", "owner2", true, 42)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    handle_install(
        mock_install_request("vm3", "owner3", true, 42)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 1);
    assert!(install_queue.contains(&42));

    let expected_vm_ids = [
        VmId::new("vm", "owner"),
        VmId::new("vm", "owner2"),
        VmId::new("vm3", "owner3"),
    ];
    for expected_vm_id in expected_vm_ids {
        let mount_queue = get_mount_queue(mount_map.clone(), &expected_vm_id).await?;
        assert_eq!(mount_queue.len(), 1);
        assert!(mount_queue.contains(&42));
    }

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_nomount_single() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    handle_install(
        mock_install_request("vm", "owner", false, 42)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn,
    )
    .await?;

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 1);
    assert!(install_queue.contains(&42));

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 0);

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_nomount_multiple() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(2)
        .returning(|| Ok("".to_string()));

    handle_install(
        mock_install_request("vm", "owner", false, 42)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    handle_install(
        mock_install_request("vm", "owner", false, 1337)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn,
    )
    .await?;

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 2);
    assert_eq!(install_queue.get(0), Some(&1337));
    assert_eq!(install_queue.get(1), Some(&42));

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 0);

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_nomount_too_many() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(10)
        .returning(|| Ok("".to_string()));

    for i in 0..10 {
        handle_install(
            mock_install_request("vm", "owner", false, i)?,
            mount_map.clone(),
            dlc_queue.clone(),
            mock_conn.clone(),
        )
        .await?;
    }

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 5);
    assert_eq!(install_queue.get(0), Some(&9));
    assert_eq!(install_queue.get(1), Some(&8));
    assert_eq!(install_queue.get(2), Some(&7));
    assert_eq!(install_queue.get(3), Some(&6));
    assert_eq!(install_queue.get(4), Some(&5));

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 0);

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_nomount_identical() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(10)
        .returning(|| Ok("".to_string()));

    for _ in 0..10 {
        handle_install(
            mock_install_request("vm", "owner", false, 42)?,
            mount_map.clone(),
            dlc_queue.clone(),
            mock_conn.clone(),
        )
        .await?;
    }

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 1);
    assert_eq!(install_queue.get(0), Some(&42));

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 0);

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_mixed_mounted_unmounted_owners() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 3, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(5)
        .returning(|| Ok("".to_string()));

    handle_install(
        mock_install_request("vm", "owner", true, 42)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    handle_install(
        mock_install_request("vm", "owner2", false, 42)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    handle_install(
        mock_install_request("vm", "owner2", true, 1234)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    handle_install(
        mock_install_request("vm", "owner2", true, 1337)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    handle_install(
        mock_install_request("vm3", "owner3", false, 1337)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn.clone(),
    )
    .await?;

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 3);
    assert_eq!(install_queue.get(0), Some(&1337));
    assert_eq!(install_queue.get(1), Some(&1234));
    assert_eq!(install_queue.get(2), Some(&42));

    let vm_id = VmId::new("vm", "owner");
    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 1);
    assert!(mount_queue.contains(&42));

    let vm_id = VmId::new("vm", "owner2");
    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 2);
    assert!(mount_queue.contains(&1234));
    assert!(mount_queue.contains(&1337));

    let vm_id = VmId::new("vm3", "owner3");
    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 0);

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_mount_too_many() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(10)
        .returning(|| Ok("".to_string()));

    for i in 0..10 {
        handle_install(
            mock_install_request("vm", "owner", true, i)?,
            mount_map.clone(),
            dlc_queue.clone(),
            mock_conn.clone(),
        )
        .await?;
    }

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 5);
    assert_eq!(install_queue.get(0), Some(&9));
    assert_eq!(install_queue.get(1), Some(&8));
    assert_eq!(install_queue.get(2), Some(&7));
    assert_eq!(install_queue.get(3), Some(&6));
    assert_eq!(install_queue.get(4), Some(&5));

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 5);
    for i in 5..10 {
        assert!(mount_queue.contains(&i));
    }

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_mount_identical() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(10)
        .returning(|| Ok("".to_string()));

    for _ in 0..10 {
        handle_install(
            mock_install_request("vm", "owner", true, 42)?,
            mount_map.clone(),
            dlc_queue.clone(),
            mock_conn.clone(),
        )
        .await?;
    }

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 1);
    assert_eq!(install_queue.get(0), Some(&42));

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 1);
    assert!(mount_queue.contains(&42));

    Ok(())
}

#[tokio::test]
#[serial]
async fn install_dlc_already_installed() -> Result<()> {
    let game_id = mock_shader_cache_dlc()?;

    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 0);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    handle_install(
        mock_install_request("vm", "owner", false, game_id)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn,
    )
    .await?;

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 0);

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 0);

    clean_up_mock_shader_cache_dlc(game_id)?;
    Ok(())
}

#[tokio::test]
#[serial]
async fn install_mount_already_installed() -> Result<()> {
    let game_id = mock_shader_cache_dlc()?;

    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let mock_conn = mock_concierge_connection(&mock_gpu_cache, 1, 1);
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let vm_id = VmId::new("vm", "owner");

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(2)
        .returning(|| Ok("".to_string()));

    let bind_mount_context = mount_ops::helpers::mock_privileged_ops::bind_mount_context();
    let mock_gpu_cache_path = mock_gpu_cache.path().to_path_buf();
    bind_mount_context.expect().returning(move |src, dst| {
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

    handle_install(
        mock_install_request("vm", "owner", true, game_id)?,
        mount_map.clone(),
        dlc_queue.clone(),
        mock_conn,
    )
    .await?;

    let dlc_queue_read = dlc_queue.read().await;
    let install_queue = dlc_queue_read.get_install_queue();
    assert_eq!(install_queue.len(), 0);

    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 0);

    assert!(foz_db_list_contains(&mock_gpu_cache, game_id)?);
    assert!(mount_destination_exists(&mock_gpu_cache, game_id).await?);

    clean_up_mock_shader_cache_dlc(game_id)?;
    Ok(())
}

// TODO(endlesspring): Additional tests:
// - check signal contents
// - check method call signatures
// - cases when shader cache mount map is already populated
// - already mounted
