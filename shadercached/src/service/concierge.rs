// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// All interactions with vm_concierge is wrapped here. This includes both
// sending D-BUS methods and responding to signals.

use crate::dbus_constants::vm_concierge;
use crate::dbus_wrapper::DbusConnectionTrait;
use crate::shader_cache_mount::{ShaderCacheMountMapPtr, VmId};

use anyhow::Result;
use log::debug;
use std::sync::Arc;
use system_api::concierge_service::{
    AddGroupPermissionMesaRequest, GetVmGpuCachePathRequest, GetVmGpuCachePathResponse,
    VmStoppingSignal,
};

pub async fn handle_vm_stopped(
    raw_bytes: Vec<u8>,
    mount_map: ShaderCacheMountMapPtr,
) -> Result<()> {
    let stopping_signal: VmStoppingSignal = protobuf::Message::parse_from_bytes(&raw_bytes)
        .map_err(|e| dbus::MethodErr::invalid_arg(&e))?;
    let vm_id = VmId {
        vm_name: stopping_signal.name,
        vm_owner_id: stopping_signal.owner_id,
    };

    mount_map.clear_all_mounts(Some(vm_id)).await?;

    Ok(())
}

pub async fn get_vm_gpu_cache_path<D: DbusConnectionTrait>(
    vm_id: &VmId,
    dbus_conn: Arc<D>,
) -> Result<String> {
    let mut request = GetVmGpuCachePathRequest::new();
    request.name = vm_id.vm_name.to_owned();
    request.owner_id = vm_id.vm_owner_id.to_owned();
    let request_bytes = protobuf::Message::write_to_bytes(&request)?;

    let (response_bytes,): (Vec<u8>,) = dbus_conn
        .call_dbus_method(
            vm_concierge::SERVICE_NAME,
            vm_concierge::PATH_NAME,
            vm_concierge::INTERFACE_NAME,
            vm_concierge::GET_VM_GPU_CACHE_PATH_METHOD,
            (request_bytes,),
        )
        .await?;

    let response: GetVmGpuCachePathResponse = protobuf::Message::parse_from_bytes(&response_bytes)?;

    Ok(response.path)
}

pub async fn add_shader_cache_group_permission<D: DbusConnectionTrait>(
    vm_id: &VmId,
    dbus_conn: Arc<D>,
) -> Result<()> {
    let mut request = AddGroupPermissionMesaRequest::new();
    request.name = vm_id.vm_name.to_owned();
    request.owner_id = vm_id.vm_owner_id.to_owned();
    let request_bytes = protobuf::Message::write_to_bytes(&request)?;

    debug!("Requesting concierge to add group permission");
    dbus_conn
        .call_dbus_method(
            vm_concierge::SERVICE_NAME,
            vm_concierge::PATH_NAME,
            vm_concierge::INTERFACE_NAME,
            vm_concierge::ADD_GROUP_PERMISSION_MESA_METHOD,
            (request_bytes,),
        )
        .await?;

    Ok(())
}
