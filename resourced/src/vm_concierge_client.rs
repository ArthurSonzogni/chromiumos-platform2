// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::os::fd::OwnedFd;
use std::sync::Arc;
use std::time::Duration;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use dbus::nonblock::Proxy;
use dbus::nonblock::SyncConnection;
use protobuf::Message;
use system_api::concierge_service::GetVmMemoryManagementKillsConnectionRequest;
use system_api::concierge_service::GetVmMemoryManagementKillsConnectionResponse;

/// A client connecting to the org.chromium.VmConcierge dbus service
#[derive(Clone)]
pub struct VmConciergeClient {
    proxy: Proxy<'static, Arc<SyncConnection>>,
}

impl VmConciergeClient {
    /// Construct a new VmConciergeClient.
    pub fn new(conn: Arc<SyncConnection>, method_timeout: Duration) -> Self {
        Self {
            proxy: Proxy::new(
                "org.chromium.VmConcierge",
                "/org/chromium/VmConcierge",
                method_timeout,
                conn,
            ),
        }
    }

    /// Execute the GetVmMemoryManagementKillsConnection function.
    ///
    /// https://chromium.googlesource.com/chromiumos/platform2/+/498f8df27677be6a493fe62a2be826e923cd317e/vm_tools/dbus_bindings/org.chromium.VmConcierge.xml#337
    pub async fn get_vm_memory_management_connection(&self) -> Result<(OwnedFd, Duration)> {
        let request = GetVmMemoryManagementKillsConnectionRequest::new();
        let (response, mut fds): (Vec<u8>, Vec<OwnedFd>) = self
            .proxy
            .method_call(
                "org.chromium.VmConcierge",
                "GetVmMemoryManagementKillsConnection",
                (request
                    .write_to_bytes()
                    .context("failed to serialize message")?,),
            )
            .await
            .context("calling GetVmMemoryManagementKillsConnection failed")?;
        let response = GetVmMemoryManagementKillsConnectionResponse::parse_from_bytes(&response)
            .context("failed to parse response")?;
        if !response.success {
            bail!(
                "Failed to get VMMS kills connection {}",
                response.failure_reason
            );
        }
        let Some(fd) = fds.pop() else {
            bail!("Missing kill connection fd");
        };
        Ok((
            fd,
            Duration::from_millis(response.host_kill_request_timeout_ms as u64),
        ))
    }
}
