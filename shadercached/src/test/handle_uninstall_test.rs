// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use system_api::shadercached::UninstallRequest;

use crate::common::SteamAppId;
use crate::dlc_queue::new_queue;
use crate::service::handle_uninstall;

fn mock_uninstall_request(game_id: SteamAppId) -> UninstallRequest {
    let mut uninstall_request = UninstallRequest::new();
    uninstall_request.steam_app_id = game_id;
    uninstall_request
}

#[tokio::test]
async fn uninstall_single() -> Result<()> {
    let dlc_queue = new_queue();

    let uninstall_request = mock_uninstall_request(1337);
    let raw_bytes = protobuf::Message::write_to_bytes(&uninstall_request)?;
    handle_uninstall(raw_bytes, dlc_queue.clone()).await?;

    let dlc_queue_read = dlc_queue.read().await;
    let uninstall_queue = dlc_queue_read.get_uninstall_queue();
    assert_eq!(uninstall_queue.len(), 1);
    assert!(uninstall_queue.contains(&1337));

    Ok(())
}

#[tokio::test]
async fn uninstall_multiple() -> Result<()> {
    let dlc_queue = new_queue();

    let uninstall_request = mock_uninstall_request(1337);
    let raw_bytes = protobuf::Message::write_to_bytes(&uninstall_request)?;
    handle_uninstall(raw_bytes, dlc_queue.clone()).await?;

    let uninstall_request = mock_uninstall_request(42);
    let raw_bytes = protobuf::Message::write_to_bytes(&uninstall_request)?;
    handle_uninstall(raw_bytes, dlc_queue.clone()).await?;

    let uninstall_request = mock_uninstall_request(123);
    let raw_bytes = protobuf::Message::write_to_bytes(&uninstall_request)?;
    handle_uninstall(raw_bytes, dlc_queue.clone()).await?;

    let dlc_queue_read = dlc_queue.read().await;
    let uninstall_queue = dlc_queue_read.get_uninstall_queue();
    assert_eq!(uninstall_queue.len(), 3);
    assert!(uninstall_queue.contains(&1337));
    assert!(uninstall_queue.contains(&42));
    assert!(uninstall_queue.contains(&123));

    Ok(())
}
