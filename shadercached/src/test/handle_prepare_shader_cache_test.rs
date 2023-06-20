// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use system_api::shadercached::{PrepareShaderCacheRequest, PrepareShaderCacheResponse};

use crate::common::{CRYPTO_HOME, PRECOMPILED_CACHE_DIR};
use crate::service::handle_prepare_shader_cache;
use crate::service::helper::unsafe_ops::mock_quota;
use crate::shader_cache_mount::new_mount_map;

fn mock_prepare_request(vm_name: String, vm_owner_id: String) -> PrepareShaderCacheRequest {
    let mut prepare_request = PrepareShaderCacheRequest::new();
    prepare_request.vm_name = vm_name;
    prepare_request.vm_owner_id = vm_owner_id;
    prepare_request
}

#[tokio::test]
async fn prepare_shader_cache() -> Result<()> {
    let mount_map = new_mount_map();

    let vm_name_encoded = base64::encode_config("vm", base64::URL_SAFE);
    let expected_path = CRYPTO_HOME
        .join("owner")
        .join(PRECOMPILED_CACHE_DIR)
        .join(&vm_name_encoded);

    let set_quota_normal_context = mock_quota::set_quota_normal_context();

    let expected_path_clone = expected_path.clone();
    set_quota_normal_context.expect().return_once(move |p| {
        assert_eq!(p, expected_path_clone);
        Ok(())
    });
    let prepare_request = mock_prepare_request("vm".to_string(), "owner".to_string());
    let raw_bytes = protobuf::Message::write_to_bytes(&prepare_request)?;

    let response_bytes = handle_prepare_shader_cache(raw_bytes, mount_map).await?;
    let response: PrepareShaderCacheResponse =
        protobuf::Message::parse_from_bytes(&response_bytes)?;
    assert_eq!(
        response.precompiled_cache_path,
        expected_path.to_str().unwrap()
    );

    Ok(())
}
