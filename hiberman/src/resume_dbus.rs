// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Handles the D-Bus interface for resume.
use std::sync::mpsc::channel;
use std::thread;
use std::time::Duration;

use anyhow::{Context, Result};
use dbus::blocking::Connection;
use dbus::channel::MatchingReceiver; // For start_receive
use dbus::message::MatchRule;
use dbus_crossroads::Crossroads;
use libchromeos::secure_blob::SecureBlob;
use log::{debug, error};
use protobuf::Message;
use system_api::client::OrgChromiumUserDataAuthInterface; // For get_hibernate_secret
use system_api::rpc::AccountIdentifier;
use system_api::UserDataAuth::GetHibernateSecretReply;
use system_api::UserDataAuth::GetHibernateSecretRequest;
use zeroize::Zeroize;

const HIBERMAN_DBUS_NAME: &str = "org.chromium.Hibernate";
const HIBERMAN_DBUS_PATH: &str = "/org/chromium/Hibernate";
const HIBERMAN_RESUME_DBUS_INTERFACE: &str = "org.chromium.HibernateResumeInterface";

pub enum ResumeRequest {
    ResumeAccountId { account_id: String },
    ResumeAuthSessionId { auth_session_id: Vec<u8> },
    Abort { reason: String },
}

// Returns ResumeRequest when receiving D-Bus resume events.
//
// There are 2 channels for communication between the main thread and the D-Bus server thread. The
// main thread notifies resume completion to the D-Bus thread via the completion channel
// (completion_{sender|receiver}). The D-Bus thread sends the resume request to the main thread via
// the resume request channel (resume_{sender|receiver}).
//
// The resume request channel's type is std::sync::mpsc::channel (multi producer single consumer
// channel) because the sender needs to be cloned for multiple D-Bus methods. The completion
// channel's type is crossbeam_channel::Receiver (multi producer multi consumer channel) because
// the receiver needs to be cloned for multiple D-Bus methods.
//
// Reference: dbus_crossroads example:
//   https://github.com/diwic/dbus-rs/tree/master/dbus-crossroads/examples
pub fn wait_for_resume_dbus_event(
    completion_receiver: crossbeam_channel::Receiver<()>,
) -> Result<ResumeRequest> {
    // Clone the completion_receiver for multiple closures. Each closure needs to own its receiver.
    let completion_as_receiver = completion_receiver.clone();
    let completion_abort_receiver = completion_receiver.clone();

    // resume_sender is used by the D-Bus server thread to send the D-Bus resume request to main
    // thread.
    let (resume_sender, resume_receiver) = channel();
    let resume_as_sender = resume_sender.clone();
    let abort_sender = resume_sender.clone();

    let conn = Connection::new_system().context("Failed to start local dbus connection")?;
    conn.request_name(HIBERMAN_DBUS_NAME, false, false, false)
        .context("Failed to request dbus name")?;

    let mut crossroads = Crossroads::new();
    // Build a new HibernateResumeInterface.
    let iface_token = crossroads.register(HIBERMAN_RESUME_DBUS_INTERFACE, |b| {
        b.method(
            "ResumeFromHibernate",
            ("account_id",),
            (),
            move |_, _, (account_id,): (String,)| {
                // Send the resume request to the main thread.
                if let Err(e) = resume_sender.send(ResumeRequest::ResumeAccountId { account_id }) {
                    error!(
                        "Failed to send resume account id request to the main thread: {:?}",
                        e
                    );
                }
                // recv() returns an error when the sender is dropped.
                _ = completion_receiver.recv();
                debug!("ResumeFromHibernate completing");
                Ok(())
            },
        );

        b.method(
            "ResumeFromHibernateAS",
            ("auth_session_id",),
            (),
            move |_, _, (auth_session_id,): (Vec<u8>,)| {
                // Send the resume request to the main thread.
                if let Err(e) =
                    resume_as_sender.send(ResumeRequest::ResumeAuthSessionId { auth_session_id })
                {
                    error!(
                        "Failed to send resume auth session id request to the main thread: {:?}",
                        e
                    );
                }
                // recv() returns an error when the sender is dropped.
                _ = completion_as_receiver.recv();
                debug!("ResumeFromHibernateAS completing");
                Ok(())
            },
        );

        b.method(
            "AbortResume",
            ("reason",),
            (),
            move |_, _, (reason,): (String,)| {
                // Send the resume abort request to the main thread.
                if let Err(e) = abort_sender.send(ResumeRequest::Abort { reason }) {
                    error!(
                        "Failed to send resume abort request to the main thread: {:?}",
                        e
                    );
                }
                // recv() returns an error when the sender is dropped.
                _ = completion_abort_receiver.recv();
                debug!("AbortResume completing");
                Ok(())
            },
        );
    });

    // Use an empty context object as we don't have shared state.
    struct ResumeDbusContext {}
    crossroads.insert(HIBERMAN_DBUS_PATH, &[iface_token], ResumeDbusContext {});

    // The D-Bus methods are handled by the crossroads instance.
    conn.start_receive(
        MatchRule::new_method_call(),
        Box::new(move |msg, conn| {
            if let Err(e) = crossroads.handle_message(msg, conn) {
                error!("Failed to handle message: {:?}", e);
                false
            } else {
                true
            }
        }),
    );

    // Spawn a thread to process D-Bus messages.
    thread::spawn(move || loop {
        // When conn.process() times out, it returns Ok(false) and re-enters the loop. Longer
        // timeout makes this D-Bus server thread waking up less when it's waiting for the D-Bus
        // message (e.g. the user might not sign in for thirty minutes).
        if let Err(e) = conn.process(Duration::from_secs(30)) {
            error!("Failed to process dbus message: {:?}", e);
        }
    });

    let resume_request = resume_receiver.recv()?;
    Ok(resume_request)
}

// Define the timeout to connect to the dbus system.
const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(10);

/// Ask cryptohome for the hibernate key for the given account. This call only works once, then
/// cryptohome forgets the key. The return value's type is SecureBlob so its content is zeroed when
/// no longer needed.
pub fn get_user_key(account_id: &str, auth_session_id: &[u8]) -> Result<SecureBlob> {
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
    let mut account_identifier = AccountIdentifier::new();
    account_identifier.set_account_id(account_id.to_string());
    proto.account_id = Some(account_identifier).into();
    proto.auth_session_id = auth_session_id.to_vec();
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

/// Send an abort request over dbus to cancel a pending resume. The hiberman process calling this
/// function might not be the same as the hiberman process serving the dbus requests. For example,
/// a developer may invoke the abort resume subcommand.
pub fn send_abort(reason: &str) -> Result<()> {
    let conn = Connection::new_system().context("Failed to connect to dbus for send abort")?;
    let proxy = conn.with_proxy(HIBERMAN_DBUS_NAME, HIBERMAN_DBUS_PATH, DEFAULT_DBUS_TIMEOUT);

    proxy
        .method_call(HIBERMAN_RESUME_DBUS_INTERFACE, "AbortResume", (reason,))
        .context("Failed to send abort request")?;
    debug!("Sent AbortResume request");
    Ok(())
}
