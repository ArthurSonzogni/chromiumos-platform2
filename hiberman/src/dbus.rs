// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the D-Bus interface for hibernate.

use std::sync::Arc;
use std::thread;
use std::time::Duration;

use anyhow::{Context as AnyhowContext, Result};
use dbus::blocking::Connection;
use dbus::channel::MatchingReceiver;
use dbus::message::MatchRule;
use dbus_crossroads::{Context, Crossroads};
use log::{debug, error, info};
use sync::Mutex;

use crate::hiberutil::HibernateError;

/// Define the minimum acceptable seed material length.
const MINIMUM_SEED_SIZE: usize = 32;

/// Define the context shared between dbus calls. These must all have the Send
/// trait.
#[derive(Default)]
struct HibernateDbusStateInternal {
    call_count: u32,
    seed_material: Vec<u8>,
}

impl HibernateDbusStateInternal {
    /// D-bus method called by cryptohome to set secret seed material derived
    /// from user authentication.
    fn set_seed_material(&mut self, seed: &[u8]) {
        info!("Received {} bytes of seed material", seed.len());
        self.call_count += 1;
        self.seed_material = seed.to_owned();
    }
}

/// Define the d-bus state. Arc and Mutex are needed because crossroads takes
/// ownership of the state passed in, and requires the Send trait.
#[derive(Clone)]
struct HibernateDbusState(Arc<Mutex<HibernateDbusStateInternal>>);

impl HibernateDbusState {
    fn new() -> Self {
        HibernateDbusState(Arc::new(Mutex::new(HibernateDbusStateInternal::default())))
    }
}

/// Define the connection details to Dbus. This is the unprotected version, to
/// be manipulated after acquiring the lock.
pub struct HiberDbusConnectionInternal {
    conn: Connection,
    state: HibernateDbusState,
}

impl HiberDbusConnectionInternal {
    /// Fire up a new system d-bus connection. Attempt to register the
    /// well-known HibernateSeed name so cryptohome can call us. Create the
    /// HibernateSeedInterface here as well.
    pub fn new() -> Result<Self> {
        info!("Setting up dbus");
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

        // Let's add the "/seed" path, which implements the
        // org.chromium.HibernateSeed interface, to the crossroads instance.
        let state = HibernateDbusState::new();
        crossroads.insert("/org/chromium/HibernateSeed", &[iface_token], state.clone());
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
    /// seed has been received. At that point we drop off since that's all we
    /// need.
    pub fn receive_seed(&mut self) -> Result<()> {
        info!("Looping to receive seed");
        loop {
            self.conn
                .process(Duration::from_millis(30000))
                .context("Failed to process")?;
            let HibernateDbusState(state) = &self.state;
            let state = state.lock();
            if state.call_count > 0 {
                break;
            }

            debug!("Still waiting for seed");
        }

        Ok(())
    }
}

/// Define the thread safe version of the dbus connection state.
pub struct HiberDbusConnection {
    internal: Arc<Mutex<HiberDbusConnectionInternal>>,
    thread: Option<thread::JoinHandle<()>>,
}

impl HiberDbusConnection {
    /// Create a new dbus connection and announce ourselves on the bus. This
    /// function does not start serving requests yet though.
    pub fn new() -> Result<Self> {
        Ok(HiberDbusConnection {
            internal: Arc::new(Mutex::new(HiberDbusConnectionInternal::new()?)),
            thread: None,
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

    /// Block waiting for the dbus server thread to finish, which happens after
    /// someone (eg cryptohome) has called our SetSeedMaterial method to hand us
    /// key material. Then return that material.
    pub fn get_seed_material(&mut self) -> Result<Vec<u8>> {
        info!("Waiting for dbus server thread");
        // Wait for the dbus thread to exit.
        if let Some(thread) = self.thread.take() {
            thread.join().unwrap();
        }

        // Now grab the internal connection, grab the state, and grab the seed
        // material.
        let internal = self.internal.lock();
        let HibernateDbusState(state) = &internal.state;
        let state = state.lock();
        let length = state.seed_material.len();
        if length < MINIMUM_SEED_SIZE {
            return Err(HibernateError::DbusError(format!(
                "Seed size {} was below minium {}",
                length, MINIMUM_SEED_SIZE
            )))
            .context("Failed to receive seed");
        }

        info!("Got {} bytes of seed material", length);
        Ok(state.seed_material.clone())
    }

    /// Returns true if seed material is already acquired. Unlike
    /// get_seed_material() this function does not block if the seed material is
    /// not available.
    pub fn has_seed_material(&self) -> bool {
        let internal = self.internal.lock();
        let HibernateDbusState(state) = &internal.state;
        // Attempt to get the lock on the inner state. Since the thread holds it
        // while it's running, and exits as soon as the seed material is given,
        // assume a failure to acquire means no seed material is present yet.
        let state = match state.try_lock() {
            Ok(s) => s,
            Err(_) => return false,
        };

        state.seed_material.len() >= MINIMUM_SEED_SIZE
    }
}
