// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use anyhow::Result;
use serial_test::serial;
use system_api::shadercached::PurgeRequest;

use crate::common::{CRYPTO_HOME, PRECOMPILED_CACHE_DIR};
use crate::dbus_wrapper::{dbus_constants, MockDbusConnectionTrait};
use crate::service::handle_purge;
use crate::shader_cache_mount::{mount_ops, new_mount_map, VmId};
use crate::test::common::{
    add_shader_cache_mount, enqueue_mount, get_mount_queue, mock_gpucache,
    populate_precompiled_cache,
};

fn mock_purge_request(vm_name: String, vm_owner_id: String) -> PurgeRequest {
    let mut purge_request = PurgeRequest::new();
    purge_request.vm_name = vm_name;
    purge_request.vm_owner_id = vm_owner_id;
    purge_request
}

fn mock_dbus_conn() -> Arc<MockDbusConnectionTrait> {
    let mut mock_conn = MockDbusConnectionTrait::new();

    mock_conn
        .expect_call_dbus_method()
        .returning(|_, _, _, method, _: ()| {
            let installed_list: Vec<String> = vec![
                "borealis-shader-cache-42-dlc-batrider".to_string(),
                "borealis-shader-cache-1337-dlc-batrider".to_string(),
            ];
            if method == dbus_constants::dlc_service::GET_INSTALLED_METHOD {
                return Box::pin(async { Ok((installed_list,)) });
            }
            unreachable!();
        });
    mock_conn
        .expect_call_dbus_method()
        .times(2)
        .returning(|_, _, _, method, (id,): (String,)| {
            let installed_list: Vec<String> = vec![
                "borealis-shader-cache-42-dlc-batrider".to_string(),
                "borealis-shader-cache-1337-dlc-batrider".to_string(),
            ];
            if method == dbus_constants::dlc_service::UNINSTALL_METHOD {
                assert!(installed_list.contains(&id));
                return Box::pin(async { Ok(()) });
            }

            unreachable!();
        });
    Arc::new(mock_conn)
}

fn precompiled_cache_empty(vm_id: &VmId) -> Result<bool> {
    let base_path = CRYPTO_HOME
        .join(&vm_id.vm_owner_id)
        .join(PRECOMPILED_CACHE_DIR);
    let read_result = std::fs::read_dir(base_path)?;
    Ok(read_result.count() == 0)
}

#[tokio::test]
#[serial]
async fn purge_one_vm() -> Result<()> {
    let vm_id = VmId::new("vm", "owner");
    let mock_gpu_cache = mock_gpucache()?;
    // Expect DLC service dbus calls
    let dbus_conn = mock_dbus_conn();
    let mount_map = new_mount_map();

    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;
    populate_precompiled_cache(&[&vm_id])?;
    enqueue_mount(mount_map.clone(), &vm_id, 42).await?;
    assert!(!precompiled_cache_empty(&vm_id)?);

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    let prepare_request = mock_purge_request("vm".to_string(), "owner".to_string());
    let raw_bytes = protobuf::Message::write_to_bytes(&prepare_request)?;

    handle_purge(raw_bytes, mount_map.clone(), dbus_conn).await?;

    // Check if queue has been cleared
    assert_eq!(get_mount_queue(mount_map.clone(), &vm_id).await?.len(), 0);
    // Check if precompiled cache directory is clean
    assert!(precompiled_cache_empty(&vm_id)?);

    Ok(())
}

#[tokio::test]
#[serial]
async fn purge_no_vm_in_mount_map() -> Result<()> {
    let vm_id = VmId::new("vm", "owner");
    let _mock_gpu_cache = mock_gpucache()?;
    // Expect DLC service dbus calls
    let dbus_conn = mock_dbus_conn();
    let mount_map = new_mount_map();

    populate_precompiled_cache(&[&vm_id])?;
    assert!(!precompiled_cache_empty(&vm_id)?);

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    let prepare_request = mock_purge_request("vm".to_string(), "owner".to_string());
    let raw_bytes = protobuf::Message::write_to_bytes(&prepare_request)?;

    handle_purge(raw_bytes, mount_map.clone(), dbus_conn).await?;

    // Check if precompiled cache directory is clean
    assert!(precompiled_cache_empty(&vm_id)?);

    Ok(())
}

#[tokio::test]
#[serial]
async fn purge_many_vms() -> Result<()> {
    let vm_id = VmId::new("vm", "owner");
    let vm_id2 = VmId::new("vm", "owner2");
    let mock_gpu_cache = mock_gpucache()?;
    let dbus_conn = Arc::new(MockDbusConnectionTrait::new());
    let mount_map = new_mount_map();

    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id2).await?;
    assert_eq!(mount_map.read().await.len(), 2);
    populate_precompiled_cache(&[&vm_id, &vm_id2])?;
    assert!(!precompiled_cache_empty(&vm_id)?);
    assert!(!precompiled_cache_empty(&vm_id2)?);

    enqueue_mount(mount_map.clone(), &vm_id, 42).await?;

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .return_once(|| Ok("".to_string()));

    let prepare_request = mock_purge_request("vm".to_string(), "owner".to_string());
    let raw_bytes = protobuf::Message::write_to_bytes(&prepare_request)?;

    handle_purge(raw_bytes, mount_map.clone(), dbus_conn).await?;

    // Queue still as-is
    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert_eq!(mount_queue.len(), 1);
    assert!(mount_queue.contains(&42));
    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id2).await?;
    assert_eq!(mount_queue.len(), 0);

    // Check if precompiled cache directory is clean
    assert!(precompiled_cache_empty(&vm_id)?);
    // vm_id2 is as-is, purge request sent only for vm_id
    assert!(!precompiled_cache_empty(&vm_id2)?);

    Ok(())
}
