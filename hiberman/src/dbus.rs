// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the D-Bus interface for hibernate.

use std::ops::{Deref, DerefMut};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

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
use zeroize::{Zeroize, ZeroizeOnDrop};

use crate::hiberutil::HibernateError;

/// Define the minimum acceptable seed material length.
const MINIMUM_SEED_SIZE: usize = 32;

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
    account_id: String,
    auth_session_id: Vec<u8>,
    completion_tx: Sender<u32>,
    when: Instant,
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

/// Define the message sent if the resume is aborted.
struct ResumeAbort {
    reason: String,
}

/// Define the union used to receive either a ResumeRequest or a ResumeAbort.
enum ResumeVerdict {
    Requested(ResumeRequest),
    Aborted(ResumeAbort),
}

/// Define the context shared between dbus calls. These must all have the Send
/// trait.
struct HibernateDbusStateInternal {
    call_count: u32,
    resume_tx: Sender<ResumeVerdict>,
    stop: bool,
}

impl HibernateDbusStateInternal {
    fn new(resume_tx: Sender<ResumeVerdict>) -> Self {
        Self {
            call_count: 0,
            resume_tx,
            stop: false,
        }
    }

    /// D-bus method called by Chrome to let the hibernate service
    /// know a user session is about to be started.
    fn resume_from_hibernate(&mut self, account_id: &str, auth_session_id: &[u8]) {
        self.call_count += 1;
        let (completion_tx, completion_rx) = channel();
        let request = ResumeRequest {
            account_id: account_id.to_string(),
            auth_session_id: auth_session_id.to_vec(),
            completion_tx,
            when: Instant::now(),
        };

        let _ = self.resume_tx.send(ResumeVerdict::Requested(request));

        // Wait on the completion channel for the main thread to send something.
        // It doesn't matter what it is, and on error, just keep going.
        info!("ResumeFromHibernate: waiting on main thread");
        let _ = completion_rx.recv();
    }

    /// D-bus method called by Chrome to initiate resume from hibernation, given
    /// an account ID.
    fn resume_from_hibernate_acct(&mut self, account_id: &str) {
        self.resume_from_hibernate(account_id, &[])
    }

    /// D-bus method called by Chrome to initiate resume from hibernation within
    /// an auth session.
    fn resume_from_hibernate_auth(&mut self, auth_session_id: &[u8]) {
        self.resume_from_hibernate("", auth_session_id)
    }

    /// D-bus method called by various components in the system to abort a
    /// resume from hibernation.
    fn abort_resume(&mut self, reason: String) {
        self.call_count += 1;
        info!("Received abort request: {}", &reason);
        let _ = self
            .resume_tx
            .send(ResumeVerdict::Aborted(ResumeAbort { reason }));
    }
}

/// Define the d-bus state. Arc and Mutex are needed because crossroads takes
/// ownership of the state passed in, and requires the Send trait.
#[derive(Clone)]
struct HibernateDbusState(Arc<Mutex<HibernateDbusStateInternal>>);

impl HibernateDbusState {
    fn new(resume_tx: Sender<ResumeVerdict>) -> Self {
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
    fn new(resume_tx: Sender<ResumeVerdict>) -> Result<Self> {
        info!("Setting up dbus");
        let state = HibernateDbusState::new(resume_tx);
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
                move |_ctx: &mut Context,
                      state: &mut HibernateDbusState,
                      (account_id,): (String,)| {
                    // Here's what happens when the method is called.
                    let mut internal_state = state.0.lock();
                    internal_state.resume_from_hibernate_acct(&account_id);
                    // Shut down the dbus thread since resume is committed.
                    internal_state.stop = true;
                    info!("ResumeFromHibernate completing");
                    Ok(())
                },
            );

            b.method(
                "ResumeFromHibernateAS",
                ("auth_session_id",),
                (),
                move |_ctx: &mut Context,
                      state: &mut HibernateDbusState,
                      (auth_session_id,): (Vec<u8>,)| {
                    // Here's what happens when the method is called.
                    let mut internal_state = state.0.lock();
                    internal_state.resume_from_hibernate_auth(&auth_session_id);
                    // Shut down the dbus thread since resume is committed.
                    internal_state.stop = true;
                    info!("ResumeFromHibernateAS completing");
                    Ok(())
                },
            );

            b.method(
                "AbortResume",
                ("reason",),
                (),
                move |_ctx: &mut Context, state: &mut HibernateDbusState, (reason,): (String,)| {
                    // Here's what happens when the method is called.
                    let mut internal_state = state.0.lock();
                    internal_state.abort_resume(reason);
                    info!("AbortResume completing");
                    Ok(())
                },
            );
        });

        crossroads.insert(HIBERMAN_DBUS_PATH, &[iface_token], state.clone());
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
    /// resume method or abort gets called. At that point we drop off since
    /// that's all we need.
    fn receive_seed(&mut self) -> Result<()> {
        info!("Processing dbus requests");
        loop {
            self.conn
                .process(Duration::from_millis(30000))
                .context("Failed to process")?;
            // Break out if ResumefromHibernate was called.
            let state = self.state.0.lock();
            if state.stop {
                info!("Stopped serving dbus requests");
                break;
            }
        }

        Ok(())
    }
}

#[derive(Default, Zeroize, ZeroizeOnDrop)]
pub struct ResumeSecretSeed {
    value: Vec<u8>,
}

impl Deref for ResumeSecretSeed {
    type Target = Vec<u8>;

    fn deref(&self) -> &Self::Target {
        &self.value
    }
}

impl DerefMut for ResumeSecretSeed {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.value
    }
}

/// This struct serves as a ticket indicating that the dbus thread is currently
/// blocked in the ResumeFromHibernate method.
pub struct PendingResumeCall {
    pub secret_seed: ResumeSecretSeed,
    pub when: Instant,
    // When this resume request is dropped, the dbus thread allows the method
    // call to complete.
    _resume_request: ResumeRequest,
}

/// Define the thread safe version of the dbus connection state.
pub struct HiberDbusConnection {
    internal: Arc<Mutex<HiberDbusConnectionInternal>>,
    thread: Option<thread::JoinHandle<()>>,
    resume_rx: Receiver<ResumeVerdict>,
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
    /// then return that material. This errors out upon receiving an abort if
    /// resume_in_progress is set, or continues waiting otherwise.
    pub fn get_seed_material(&mut self, resume_in_progress: bool) -> Result<PendingResumeCall> {
        loop {
            // Block until the dbus thread receives a ResumeFromHibernate call, and
            // receive that info.
            info!("Waiting for ResumeFromHibernate call");
            let resume_verdict = self.resume_rx.recv()?;
            match resume_verdict {
                ResumeVerdict::Aborted(resume_abort) => {
                    error!("Got resume abort request: {}", &resume_abort.reason);
                    if !resume_in_progress {
                        debug!("Ignoring abort request since resume is not in progress");
                        continue;
                    }

                    return Err(HibernateError::ResumeAbortRequested(resume_abort.reason))
                        .context("Resume aborted");
                }
                ResumeVerdict::Requested(mut resume_request) => {
                    // If there's no resume in progress, unblock boot ASAP.
                    if !resume_in_progress {
                        info!("Unblocking ResumeFromHibernate immediately");
                        resume_request.complete();
                    }

                    info!("Requesting secret seed");
                    let when = resume_request.when;
                    let mut pending_call = PendingResumeCall {
                        secret_seed: ResumeSecretSeed::default(),
                        _resume_request: resume_request,
                        when,
                    };

                    get_secret_seed(
                        &pending_call._resume_request.account_id,
                        &pending_call._resume_request.auth_session_id,
                        &mut pending_call.secret_seed.value,
                    )?;
                    let length = pending_call.secret_seed.value.len();
                    if length < MINIMUM_SEED_SIZE {
                        return Err(HibernateError::DbusError(format!(
                            "Seed size {} was below minium {}",
                            length, MINIMUM_SEED_SIZE
                        )))
                        .context("Failed to receive seed");
                    }

                    info!("Got {} bytes of seed material", length);
                    return Ok(pending_call);
                }
            }
        }
    }
}

/// Ask cryptohome for the hibernate seed for the given account. This call only
/// works once, then cryptohome forgets the secret. The vector receiving the
/// secret is passed in rather than being returned so its memory location can be
/// accounted for and explicitly zeroed out when no longer needed.
fn get_secret_seed(account_id: &str, auth_session_id: &[u8], seed: &mut Vec<u8>) -> Result<()> {
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
    proto.auth_session_id = auth_session_id.to_vec();
    let mut response = conn_path
        .get_hibernate_secret(proto.write_to_bytes().unwrap())
        .context("Failed to call GetHibernateSecret dbus method")?;
    let mut reply: GetHibernateSecretReply = Message::parse_from_bytes(&response)
        .context("Failed to parse GetHibernateSecret dbus response")?;
    response.zeroize();
    // Copy the secret to the output parameter so the reply structure can be
    // zeroed.
    seed.resize(reply.hibernate_secret.len(), 0);
    seed.copy_from_slice(&reply.hibernate_secret);
    reply.hibernate_secret.fill(0);
    Ok(())
}

/// Send an abort request over dbus to cancel a pending resume. The hiberman
/// process calling this function might not be the same as the hiberman process
/// serving the dbus requests. For example, a developer may invoke the abort
/// resume subcommand.
pub fn send_abort(reason: &str) -> Result<()> {
    let conn = Connection::new_system().context("Failed to connect to dbus for secret seed")?;
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
