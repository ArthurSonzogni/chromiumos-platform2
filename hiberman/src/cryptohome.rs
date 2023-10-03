// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use anyhow::Context;
use anyhow::Result;
use dbus::blocking::Connection;
use libchromeos::secure_blob::SecureBlob;
use protobuf::Message;
use system_api::client::OrgChromiumUserDataAuthInterface; // For get_hibernate_secret
use system_api::UserDataAuth::GetHibernateSecretReply;
use system_api::UserDataAuth::GetHibernateSecretRequest;
use zeroize::Zeroize;

// Define the timeout to connect to the dbus system.
const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

/// Ask cryptohome for the hibernate key for the given user session or account. This call only works
/// once, then cryptohome forgets the key. The return value's type is SecureBlob so its content is
/// zeroed when no longer needed.
pub fn get_user_key_for_session(session_id: &[u8]) -> Result<SecureBlob> {
    const CRYPTOHOME_DBUS_NAME: &str = "org.chromium.UserDataAuth";
    const CRYPTOHOME_DBUS_PATH: &str = "/org/chromium/UserDataAuth";

    let conn =
        Connection::new_system().context("Failed to connect to dbus for hibernate secret")?;
    let proxy = conn.with_proxy(
        CRYPTOHOME_DBUS_NAME,
        CRYPTOHOME_DBUS_PATH,
        DEFAULT_DBUS_TIMEOUT,
    );

    let mut proto: GetHibernateSecretRequest = Message::new();
    proto.auth_session_id = session_id.to_vec();

    let mut response = proxy
        .get_hibernate_secret(proto.write_to_bytes().unwrap())
        .context("Failed to call GetHibernateSecret dbus method")?;
    let mut reply: GetHibernateSecretReply = Message::parse_from_bytes(&response)
        .context("Failed to parse GetHibernateSecret dbus response")?;
    response.zeroize();

    // Copy the key to the output parameter so the reply structure can be zeroed.
    let mut key_data: Vec<u8> = vec![0; reply.hibernate_secret.len()];
    key_data.copy_from_slice(&reply.hibernate_secret);
    reply.hibernate_secret.fill(0);
    Ok(SecureBlob::from(key_data))
}
