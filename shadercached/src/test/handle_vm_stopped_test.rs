// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use system_api::concierge_service::VmStoppingSignal;

use crate::service::handle_vm_stopped;
use crate::shader_cache_mount::{new_mount_map, VmId};
use crate::test::common::{
    add_shader_cache_mount, enqueue_mount, foz_db_list_empty, get_mount_queue, mock_gpucache,
    mount_destination_exists, simulate_mounted,
};

fn mock_vm_signal(vm_id: &VmId) -> Result<Vec<u8>> {
    let mut signal = VmStoppingSignal::new();
    // CID does not matter, not used by shadercached
    signal.cid = 42;
    signal.name = vm_id.vm_name.clone();
    signal.owner_id = vm_id.vm_owner_id.clone();
    Ok(protobuf::Message::write_to_bytes(&signal)?)
}

#[tokio::test]
async fn vm_stopped_not_existing() -> Result<()> {
    let mount_map = new_mount_map();
    let vm_id = VmId::new("vm", "owner");

    assert!(handle_vm_stopped(mock_vm_signal(&vm_id)?, mount_map)
        .await
        .is_err());

    Ok(())
}

#[tokio::test]
async fn vm_stopped_clear_mount_queue_and_mounted() -> Result<()> {
    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let vm_id = VmId::new("vm", "owner");

    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;
    enqueue_mount(mount_map.clone(), &vm_id, 42).await?;
    simulate_mounted(&mock_gpu_cache, 1337).await?;

    handle_vm_stopped(mock_vm_signal(&vm_id)?, mount_map.clone()).await?;

    assert!(foz_db_list_empty(&mock_gpu_cache)?);
    assert!(!mount_destination_exists(&mock_gpu_cache, 42).await?);
    let mount_queue = get_mount_queue(mount_map.clone(), &vm_id).await?;
    assert!(mount_queue.is_empty());

    Ok(())
}
