// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the D-Bus interface for hibernate.

use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use anyhow::{Context as AnyhowContext, Result};
use dbus::blocking::Connection;
use dbus::channel::MatchingReceiver;
use dbus::message::MatchRule;
use dbus_crossroads::{Context, Crossroads};
use log::{debug, error, info};
use protobuf::{Message, SingularPtrField};
use sync::Mutex;
use system_api::client::OrgChromiumUserDataAuthInterface;
use system_api::rpc::AccountIdentifier;
use system_api::UserDataAuth::{GetHibernateSecretReply, GetHibernateSecretRequest};

use crate::hiberutil::HibernateError;

/// Define the minimum acceptable seed material length.
const MINIMUM_SEED_SIZE: usize = 32;

// Define the timeout to connect to the dbus system.
pub const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(25);

/// Define the message sent by the d-bus thread to the main thread when the
/// ResumeFromHibernate d-bus method is called.
struct ResumeRequest {
    account_id: String,
    completion_tx: Sender<u32>,
}

impl ResumeRequest {
    pub fn complete(&mut self) {
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
struct HibernateDbusStateInternal {
    call_count: u32,
    seed_material: Vec<u8>,
    resume_tx: Sender<ResumeRequest>,
    stop: bool,
}

impl HibernateDbusStateInternal {
    fn new(resume_tx: Sender<ResumeRequest>) -> Self {
        Self {
            call_count: 0,
            seed_material: vec![],
            resume_tx,
            stop: false,
        }
    }

    /// D-bus method called by cryptohome to set secret seed material derived
    /// from user authentication.
    fn set_seed_material(&mut self, seed: &[u8]) {
        info!("Received {} bytes of seed material", seed.len());
        self.call_count += 1;
        self.seed_material = seed.to_owned();
    }

    /// D-bus method called by login_manager to let the hibernate service
    /// know a user session is about to be started.
    fn resume_from_hibernate(&mut self, account_id: &str) {
        self.call_count += 1;
        let (completion_tx, completion_rx) = channel();
        let _ = self.resume_tx.send(ResumeRequest {
            account_id: account_id.to_string(),
            completion_tx,
        });

        // Wait on the completion channel for the main thread to send something.
        // It doesn't matter what it is, and on error, just keep going.
        info!("ResumeFromHibernate: waiting on main thread");
        let _ = completion_rx.recv();
    }
}

/// Define the d-bus state. Arc and Mutex are needed because crossroads takes
/// ownership of the state passed in, and requires the Send trait.
#[derive(Clone)]
struct HibernateDbusState(Arc<Mutex<HibernateDbusStateInternal>>);

impl HibernateDbusState {
    fn new(resume_tx: Sender<ResumeRequest>) -> Self {
        HibernateDbusState(Arc::new(Mutex::new(HibernateDbusStateInternal::new(
            resume_tx,
        ))))
    }
}

/// Define the connection details to Dbus. This is the unprotected version, to
/// be manipulated after acquiring the lock.
struct HiberDbusConnectionInternal {
    conn: Connection,
    state: HibernateDbusState,
}

impl HiberDbusConnectionInternal {
    /// Fire up a new system d-bus server.
    fn new(resume_tx: Sender<ResumeRequest>) -> Result<Self> {
        info!("Setting up dbus");
        let state = HibernateDbusState::new(resume_tx);
        let conn = Connection::new_system().context("Failed to start local dbus connection")?;
        conn.request_name("org.chromium.Hibernate", false, false, false)
            .context("Failed to request dbus name")?;

        let mut crossroads = Crossroads::new();
        // Build a new HibernateSeedInterface.
        let iface_token = crossroads.register("org.chromium.HibernateSeedInterface", |b| {
            // Let's add a method to the interface. We have the method name,
            // followed by names of input and output arguments (used for
            // introspection). The closure then controls the types of these
            // arguments. The last argument to the closure is a tuple of the
            // input arguments.
            b.method(
                "SetSeedMaterial",
                ("seed",),
                (),
                move |_ctx: &mut Context, state: &mut HibernateDbusState, (seed,): (Vec<u8>,)| {
                    // Here's what happens when the method is called.
                    state.0.lock().set_seed_material(&seed);
                    Ok(())
                },
            );
        });

        crossroads.insert("/org/chromium/Hibernate", &[iface_token], state.clone());
        // Build a new HibernateResumeInterface.
        let iface_token = crossroads.register("org.chromium.HibernateResumeInterface", |b| {
            b.method(
                "ResumeFromHibernate",
                ("account_id",),
                (),
                move |_ctx: &mut Context,
                      state: &mut HibernateDbusState,
                      (account_id,): (String,)| {
                    // Here's what happens when the method is called.
                    let mut internal_state = state.0.lock();
                    internal_state.resume_from_hibernate(&account_id);
                    // This is currently the only thing the dbus thread is alive
                    // for, so shut it down after this method call.
                    internal_state.stop = true;
                    info!("ResumeFromHibernate completing");
                    Ok(())
                },
            );
        });
        crossroads.insert("/org/chromium/Hibernate", &[iface_token], state.clone());
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

        info!("Completed dbus setup");
        Ok(HiberDbusConnectionInternal { conn, state })
    }

    /// Public function used by the dbus thread to process requests until the
    /// resume method gets called. At that point we drop off since that's all we
    /// need.
    fn receive_seed(&mut self) -> Result<()> {
        info!("Looping to receive ResumeFromHibernate dbus call");
        loop {
            self.conn
                .process(Duration::from_millis(30000))
                .context("Failed to process")?;
            // Break out if ResumefromHibernate was called.
            let state = self.state.0.lock();
            if state.stop {
                break;
            }

            debug!("Still waiting for ResumeFromHibernate dbus call");
        }

        Ok(())
    }
}

/// Define the thread safe version of the dbus connection state.
pub struct HiberDbusConnection {
    internal: Arc<Mutex<HiberDbusConnectionInternal>>,
    thread: Option<thread::JoinHandle<()>>,
    resume_rx: Receiver<ResumeRequest>,
}

impl HiberDbusConnection {
    /// Create a new dbus connection and announce ourselves on the bus. This
    /// function does not start serving requests yet though.
    pub fn new() -> Result<Self> {
        let (resume_tx, resume_rx) = channel();
        Ok(HiberDbusConnection {
            internal: Arc::new(Mutex::new(HiberDbusConnectionInternal::new(resume_tx)?)),
            thread: None,
            resume_rx,
        })
    }

    /// Fire up a thread to respond to dbus requests.
    pub fn spawn_dbus_server(&mut self) -> Result<()> {
        let arc_clone = Arc::clone(&self.internal);
        self.thread = Some(thread::spawn(move || {
            debug!("Started dbus server thread");
            let mut conn = arc_clone.lock();
            let _ = conn.receive_seed();
            debug!("Exiting dbus server thread");
        }));

        Ok(())
    }

    /// Block waiting for the seed material to become available from cryptohome,
    /// then return that material.
    pub fn get_seed_material(&mut self, resume_in_progress: bool) -> Result<PendingResumeCall> {
        // Block until the dbus thread receives a ResumeFromHibernate call, and
        // receive that info.
        info!("Waiting for ResumeFromHibernate call");
        let mut resume_request = self.resume_rx.recv()?;

        // If there's no resume in progress, unblock boot ASAP.
        if !resume_in_progress {
            info!("Unblocking ResumeFromHibernate immediately");
            resume_request.complete();
        }

        info!("Requesting secret seed");
        let secret_seed = get_secret_seed(&resume_request.account_id)?;
        let length = secret_seed.len();
        if length < MINIMUM_SEED_SIZE {
            return Err(HibernateError::DbusError(format!(
                "Seed size {} was below minium {}",
                length, MINIMUM_SEED_SIZE
            )))
            .context("Failed to receive seed");
        }

        info!("Got {} bytes of seed material", length);
        Ok(PendingResumeCall {
            secret_seed,
            _resume_request: resume_request,
        })
    }
}

/// This struct serves as a ticket indicating that the dbus thread is currently
/// blocked in the ResumeFromHibernate method.
pub struct PendingResumeCall {
    pub secret_seed: Vec<u8>,
    // When this resume request is dropped, the dbus thread allows the method
    // call to complete.
    _resume_request: ResumeRequest,
}

/// Ask cryptohome for the hibernate seed for the given account. This call only
/// works once, then cryptohome forgets the secret.
fn get_secret_seed(account_id: &str) -> Result<Vec<u8>> {
    let conn = Connection::new_system().context("Failed to connect to dbus for secret seed")?;
    let conn_path = conn.with_proxy(
        "org.chromium.UserDataAuth",
        "/org/chromium/UserDataAuth",
        DEFAULT_DBUS_TIMEOUT,
    );

    let mut proto: GetHibernateSecretRequest = Message::new();
    let mut account_identifier = AccountIdentifier::new();
    account_identifier.set_account_id(account_id.to_string());
    proto.account_id = SingularPtrField::some(account_identifier);
    let response = conn_path
        .get_hibernate_secret(proto.write_to_bytes().unwrap())
        .context("Failed to call GetHibernateSecret dbus method")?;
    let response: GetHibernateSecretReply = Message::parse_from_bytes(&response)
        .context("Failed to parse GetHibernateSecret dbus response")?;
    Ok(response.hibernate_secret)
}
