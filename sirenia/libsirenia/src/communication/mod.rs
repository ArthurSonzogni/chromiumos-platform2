// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the communication abstraction for sirenia. Used both for
//! communication between dugong and trichechus and between TEEs and
//! trichechus.
//!

pub mod persistence;
pub mod trichechus;

use std::fmt::Debug;
use std::io::{self, BufWriter, Read, Write};
use std::ops::Deref;
use std::result::Result as StdResult;

use flexbuffers::FlexbufferSerializer;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use sirenia_rpc_macros::sirenia_rpc;
use thiserror::Error as ThisError;

pub const LENGTH_BYTE_SIZE: usize = 4;

#[derive(Debug, ThisError)]
pub enum Error {
    #[error("failed to read: {0}")]
    Read(#[source] io::Error),
    #[error("no data to read from socket")]
    EmptyRead,
    #[error("failed to write: {0}")]
    Write(#[source] io::Error),
    #[error("error deserializing: {0}")]
    Deserialize(#[source] flexbuffers::DeserializationError),
    #[error("error serializing: {0}")]
    Serialize(#[source] flexbuffers::SerializationError),
}

/// The result of an operation in this crate.
pub type Result<T> = StdResult<T, Error>;

/// Definition for storage rpc needed both by manatee_runtime and sirenia
#[sirenia_rpc]
pub trait StorageRpc {
    type Error;

    fn read_data(&self, id: String) -> StdResult<(persistence::Status, Vec<u8>), Self::Error>;
    fn write_data(&self, id: String, data: Vec<u8>) -> StdResult<persistence::Status, Self::Error>;
}

// Reads a message from the given Read. First reads a u32 that says the length
// of the serialized message, then reads the serialized message and
// deserializes it.
pub fn read_message<R: Read, D: DeserializeOwned>(r: &mut R) -> Result<D> {
    // Read the length of the serialized message first
    let mut buf = [0; LENGTH_BYTE_SIZE];
    r.read_exact(&mut buf).map_err(Error::Read)?;

    let message_size: u32 = u32::from_be_bytes(buf);

    if message_size == 0 {
        return Err(Error::EmptyRead);
    }

    // Read the actual serialized message
    let mut ser_message = vec![0; message_size as usize];
    r.read_exact(&mut ser_message).map_err(Error::Read)?;

    flexbuffers::from_slice(&ser_message).map_err(Error::Deserialize)
}

// Writes the given message to the given Write. First writes the length of the
// serialized message then the serialized message itself.
pub fn write_message<W: Write, S: Serialize>(w: &mut W, m: S) -> Result<()> {
    let mut writer = BufWriter::new(w);

    // Serialize the message and calculate the length
    let mut ser = FlexbufferSerializer::new();
    m.serialize(&mut ser).map_err(Error::Serialize)?;

    let len: u32 = ser.view().len() as u32;

    let mut len_ser = FlexbufferSerializer::new();
    len.serialize(&mut len_ser).map_err(Error::Serialize)?;

    writer.write(&len.to_be_bytes()).map_err(Error::Write)?;
    writer.write(ser.view()).map_err(Error::Write)?;

    Ok(())
}

/// Types needed for trichechus RPC

pub const SHA256_SIZE: usize = 32;

#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash, Serialize, Deserialize)]
pub struct Digest([u8; SHA256_SIZE]);

impl Deref for Digest {
    type Target = [u8; SHA256_SIZE];

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<A: AsRef<[u8]>> From<A> for Digest {
    fn from(d: A) -> Self {
        let mut array = [0u8; SHA256_SIZE];
        array.copy_from_slice(d.as_ref());
        Digest(array)
    }
}

#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub enum ExecutableInfo {
    // Hypervisor initramfs path
    Path(String),
    // Only digest, location unspecified
    Digest(Digest),
    // Host (Chrome OS) path and digest
    CrosPath(String, Digest),
}
