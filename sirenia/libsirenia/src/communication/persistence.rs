// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines messages used for communication between Trichechus and Cronista for storing and
//! retrieving persistent data.

use std::collections::{BTreeMap as Map, VecDeque};
use std::ops::{Deref, DerefMut};
use std::sync::Mutex;

use serde::{Deserialize, Serialize};
use sirenia_rpc_macros::sirenia_rpc;

use crate::rpc;

/// Represents the possible status codes from an RPC.
/// Values are assigned to make it easier to interface with D-Bus.
#[derive(Debug, Deserialize, Serialize)]
pub enum Status {
    Success = 0,
    Failure = 1,
    /// The operation failed because the ID hasn't been written to yet.
    IdNotFound = 2,
    /// A crypto operation failed when trying to complete the operation.
    CryptoFailure = 3,
}

/// Should the data be globally available or only available within the users' sessions.
/// Values are assigned to make it easier to interface with D-Bus.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Ord, PartialOrd, Hash, Serialize)]
pub enum Scope {
    System = 0,
    Session = 1,
    Test = -1,
}

#[sirenia_rpc]
pub trait Cronista {
    type Error;

    //TODO These need to carry enough information to prove the entry was recorded in the log.
    fn persist(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
        data: Vec<u8>,
    ) -> std::result::Result<Status, Self::Error>;
    fn retrieve(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
    ) -> std::result::Result<(Status, Vec<u8>), Self::Error>;
}

#[derive(Clone, Hash, Eq, PartialEq, Ord, PartialOrd)]
struct CronistaIdentifier {
    scope: Scope,
    domain: String,
    identifier: String,
}

impl CronistaIdentifier {
    fn new(scope: Scope, domain: String, identifier: String) -> Self {
        CronistaIdentifier {
            scope,
            domain,
            identifier,
        }
    }
}

/// A in memory implementation of the Cronista interface for unit tests.
pub struct MockCronista {
    storage: Mutex<Map<CronistaIdentifier, Vec<u8>>>,
    next_error: Mutex<VecDeque<rpc::Error>>,
    fail_next: bool,
}

impl MockCronista {
    pub fn new() -> Self {
        MockCronista {
            storage: Mutex::new(Map::new()),
            next_error: Mutex::new(VecDeque::new()),
            fail_next: false,
        }
    }

    pub fn next_error_push_back(&mut self, err: rpc::Error) {
        self.next_error.lock().unwrap().deref_mut().push_back(err);
    }

    pub fn fail_next(&self) -> bool {
        self.fail_next
    }

    pub fn fail_next_mut(&mut self) -> &bool {
        &mut self.fail_next
    }
}

impl Default for MockCronista {
    fn default() -> Self {
        Self::new()
    }
}

impl Cronista for MockCronista {
    type Error = rpc::Error;

    fn persist(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
        data: Vec<u8>,
    ) -> Result<Status, Self::Error> {
        if let Some(err) = self.next_error.lock().unwrap().deref_mut().pop_front() {
            return Err(err);
        }
        if self.fail_next {
            return Ok(Status::Failure);
        }
        self.storage
            .lock()
            .unwrap()
            .deref_mut()
            .insert(CronistaIdentifier::new(scope, domain, identifier), data);
        Ok(Status::Success)
    }

    fn retrieve(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
    ) -> Result<(Status, Vec<u8>), Self::Error> {
        if let Some(err) = self.next_error.lock().unwrap().deref_mut().pop_front() {
            return Err(err);
        }
        if self.fail_next {
            return Ok((Status::Failure, Vec::default()));
        }
        match self
            .storage
            .lock()
            .unwrap()
            .deref()
            .get(&CronistaIdentifier::new(scope, domain, identifier))
        {
            Some(data) => Ok((Status::Success, data.clone())),
            None => Ok((Status::IdNotFound, Vec::default())),
        }
    }
}
