// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the D-Bus interface for hibernate.

use std::ops::Deref;
use std::ops::DerefMut;
use std::sync::mpsc::Sender;
use std::sync::Arc;
use std::time::Duration;

use anyhow::Context as AnyhowContext;
use anyhow::Result;
use dbus::blocking::Connection;
use log::info;
use sync::Mutex;
use zeroize::Zeroize;
use zeroize::ZeroizeOnDrop;

// Define the timeout to connect to the dbus system.
pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(25);

/// Define the name used on dbus.
const HIBERMAN_DBUS_NAME: &str = "org.chromium.Hibernate";
/// Define the path used within dbus.
const HIBERMAN_DBUS_PATH: &str = "/org/chromium/Hibernate";
/// Define the name of the resume dbus interface.
const HIBERMAN_RESUME_DBUS_INTERFACE: &str = "org.chromium.HibernateResumeInterface";

/// Define the message sent by the d-bus thread to the main thread when the
/// ResumeFromHibernate d-bus method is called.
struct ResumeRequest {
    completion_tx: Sender<u32>,
}

impl ResumeRequest {
    pub fn complete(&mut self) {
        info!("Completing the ResumeRequest");
        // The content of the message is ignored, this simply allows the dbus
        // thread to unblock and complete the method. The error is ignored
        // because the best course of action is simply to keep going with
        // the main thread.
        let _ = self.completion_tx.send(0);
    }
}

impl Drop for ResumeRequest {
    fn drop(&mut self) {
        self.complete();
    }
}

/// Define the context shared between dbus calls. These must all have the Send
/// trait.
struct HibernateDbusStateInternal {}

/// Define the d-bus state. Arc and Mutex are needed because crossroads takes
/// ownership of the state passed in, and requires the Send trait.
#[derive(Clone)]
struct HibernateDbusState(Arc<Mutex<HibernateDbusStateInternal>>);

#[derive(Default, Zeroize, ZeroizeOnDrop)]
pub struct HibernateKey {
    value: Vec<u8>,
}

impl HibernateKey {
    pub fn new(v: Vec<u8>) -> Self {
        Self { value: v }
    }
}

impl Deref for HibernateKey {
    type Target = Vec<u8>;

    fn deref(&self) -> &Self::Target {
        &self.value
    }
}

impl DerefMut for HibernateKey {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.value
    }
}

/// Send an abort request over dbus to cancel a pending resume. The hiberman
/// process calling this function might not be the same as the hiberman process
/// serving the dbus requests. For example, a developer may invoke the abort
/// resume subcommand.
pub fn send_abort(reason: &str) -> Result<()> {
    let conn = Connection::new_system().context("Failed to connect to dbus for send abort")?;
    let conn_path = conn.with_proxy(HIBERMAN_DBUS_NAME, HIBERMAN_DBUS_PATH, DEFAULT_DBUS_TIMEOUT);

    // Now make the method call. The ListNames method call takes zero input parameters and
    // one output parameter which is an array of strings.
    // Therefore the input is a zero tuple "()", and the output is a single tuple "(names,)".
    conn_path
        .method_call(HIBERMAN_RESUME_DBUS_INTERFACE, "AbortResume", (reason,))
        .context("Failed to send abort request")?;
    info!("Sent AbortResume request");
    Ok(())
}
