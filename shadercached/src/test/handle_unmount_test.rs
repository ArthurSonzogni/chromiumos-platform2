// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use system_api::shadercached::UnmountRequest;

use crate::common::SteamAppId;
use crate::service::handle_unmount;
use crate::shader_cache_mount::{new_mount_map, VmId};
use crate::test::common::{
    foz_db_list_contains, foz_db_list_empty, get_unmount_queue, mock_gpucache, simulate_mounted,
};

use super::common::add_shader_cache_mount;

fn mock_unmount_request(vm_name: &str, vm_owner_id: &str, game_id: SteamAppId) -> UnmountRequest {
    let mut install_request = UnmountRequest::new();
    install_request.vm_name = vm_name.to_string();
    install_request.vm_owner_id = vm_owner_id.to_string();
    install_request.steam_app_id = game_id;
    install_request
}

#[tokio::test]
async fn unmount_single() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let vm_id = VmId::new("vm", "owner");
    let mount_map = new_mount_map();
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;
    simulate_mounted(&mock_gpu_cache, 42).await?;

    assert!(foz_db_list_contains(&mock_gpu_cache, 42)?);

    let unmount_request = mock_unmount_request("vm", "owner", 42);
    let raw_bytes = protobuf::Message::write_to_bytes(&unmount_request)?;

    handle_unmount(raw_bytes, mount_map.clone()).await?;

    let mount_map_read = mount_map.read().await;
    let shader_cache_mount = mount_map_read.get(&vm_id).unwrap();
    let unmount_queue = shader_cache_mount.get_unmount_queue();
    assert_eq!(unmount_queue.len(), 1);
    assert!(unmount_queue.contains(&42));

    assert!(foz_db_list_empty(&mock_gpu_cache)?);

    Ok(())
}

#[tokio::test]
async fn unmount_single_among_many() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let vm_id = VmId::new("vm", "owner");
    let mount_map = new_mount_map();
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;
    simulate_mounted(&mock_gpu_cache, 42).await?;
    simulate_mounted(&mock_gpu_cache, 1337).await?;

    assert!(foz_db_list_contains(&mock_gpu_cache, 1337)?);
    assert!(foz_db_list_contains(&mock_gpu_cache, 42)?);

    let unmount_request = mock_unmount_request("vm", "owner", 42);
    let raw_bytes = protobuf::Message::write_to_bytes(&unmount_request)?;

    handle_unmount(raw_bytes, mount_map.clone()).await?;

    let unmount_queue = get_unmount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(unmount_queue.len(), 1);
    assert!(unmount_queue.contains(&42));

    assert!(foz_db_list_contains(&mock_gpu_cache, 1337)?);
    assert!(!foz_db_list_contains(&mock_gpu_cache, 42)?);

    Ok(())
}

#[tokio::test]
async fn unmount_multiple_vms_and_games() -> Result<()> {
    let mock_gpu_cache1 = mock_gpucache().expect("Failed to create mock gpu cache");
    let vm_id1 = VmId::new("vm", "owner");
    let mount_map1 = new_mount_map();
    add_shader_cache_mount(&mock_gpu_cache1, mount_map1.clone(), &vm_id1).await?;
    simulate_mounted(&mock_gpu_cache1, 42).await?;
    simulate_mounted(&mock_gpu_cache1, 1337).await?;

    assert!(foz_db_list_contains(&mock_gpu_cache1, 1337)?);
    assert!(foz_db_list_contains(&mock_gpu_cache1, 42)?);

    let mock_gpu_cache2 = mock_gpucache().expect("Failed to create mock gpu cache");
    let vm_id2 = VmId::new("vm", "owner2");
    let mount_map2 = new_mount_map();
    add_shader_cache_mount(&mock_gpu_cache2, mount_map2.clone(), &vm_id2).await?;
    simulate_mounted(&mock_gpu_cache2, 42).await?;

    assert!(foz_db_list_contains(&mock_gpu_cache2, 42)?);

    let mock_gpu_cache3 = mock_gpucache().expect("Failed to create mock gpu cache");
    let vm_id3 = VmId::new("vm2", "owner2");
    let mount_map3 = new_mount_map();
    add_shader_cache_mount(&mock_gpu_cache3, mount_map3.clone(), &vm_id3).await?;
    simulate_mounted(&mock_gpu_cache3, 1234).await?;

    assert!(foz_db_list_contains(&mock_gpu_cache3, 1234)?);

    // Unmount vm_id1 42
    let unmount_request = mock_unmount_request("vm", "owner", 42);
    let raw_bytes = protobuf::Message::write_to_bytes(&unmount_request)?;
    handle_unmount(raw_bytes, mount_map1.clone()).await?;

    let unmount_queue = get_unmount_queue(mount_map1.clone(), &vm_id1).await?;
    assert_eq!(unmount_queue.len(), 1);
    assert!(unmount_queue.contains(&42));

    assert!(foz_db_list_contains(&mock_gpu_cache1, 1337)?);
    assert!(!foz_db_list_contains(&mock_gpu_cache1, 42)?);
    assert!(foz_db_list_contains(&mock_gpu_cache2, 42)?);
    assert!(foz_db_list_contains(&mock_gpu_cache3, 1234)?);

    // Unmount vm_id2 42
    let unmount_request = mock_unmount_request("vm", "owner2", 42);
    let raw_bytes = protobuf::Message::write_to_bytes(&unmount_request)?;
    handle_unmount(raw_bytes, mount_map2.clone()).await?;

    let unmount_queue = get_unmount_queue(mount_map2.clone(), &vm_id2).await?;
    assert_eq!(unmount_queue.len(), 1);
    assert!(unmount_queue.contains(&42));

    assert!(foz_db_list_contains(&mock_gpu_cache1, 1337)?);
    assert!(!foz_db_list_contains(&mock_gpu_cache1, 42)?);
    assert!(foz_db_list_empty(&mock_gpu_cache2)?);
    assert!(foz_db_list_contains(&mock_gpu_cache3, 1234)?);

    Ok(())
}

#[tokio::test]
async fn unmount_not_mounted() -> Result<()> {
    let mock_gpu_cache = mock_gpucache().expect("Failed to create mock gpu cache");
    let vm_id = VmId::new("vm", "owner");
    let mount_map = new_mount_map();
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;

    let unmount_request = mock_unmount_request("vm", "owner", 42);
    let raw_bytes = protobuf::Message::write_to_bytes(&unmount_request)?;

    handle_unmount(raw_bytes, mount_map.clone()).await?;

    let unmount_queue = get_unmount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(unmount_queue.len(), 0);

    Ok(())
}
