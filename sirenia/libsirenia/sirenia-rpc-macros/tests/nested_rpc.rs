// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Verify sirenia-rpc_macros works for the intended use case.

extern crate sirenia_rpc_macros;

use std::thread::spawn;

use assert_matches::assert_matches;
use libsirenia::rpc::{self, RpcDispatcher};
use libsirenia::transport::create_transport_from_pipes;
use sirenia_rpc_macros::sirenia_rpc;

#[sirenia_rpc]
pub trait TestRpc<E> {
    fn checked_neg(&mut self, input: i32) -> Result<Option<i32>, E>;
    fn checked_add(&mut self, addend_a: i32, addend_b: i32) -> Result<Option<i32>, E>;
}

#[sirenia_rpc]
pub trait OtherRpc<E> {
    fn checked_neg(&mut self, input: i64) -> Result<Option<i64>, E>;
    fn checked_add(&mut self, addend_a: i64, addend_b: i64) -> Result<Option<i64>, E>;
}

#[sirenia_rpc]
pub trait NestedRpc<E>: TestRpc<E> + OtherRpc<E> {
    fn terminate(&mut self) -> Result<(), E>;
}

#[derive(Clone)]
struct NestedRpcServerImpl {}

impl TestRpc<()> for NestedRpcServerImpl {
    fn checked_neg(&mut self, input: i32) -> Result<Option<i32>, ()> {
        Ok(input.checked_neg())
    }

    fn checked_add(&mut self, addend_a: i32, addend_b: i32) -> Result<Option<i32>, ()> {
        Ok(addend_a.checked_add(addend_b))
    }
}

impl OtherRpc<()> for NestedRpcServerImpl {
    fn checked_neg(&mut self, input: i64) -> Result<Option<i64>, ()> {
        Ok(input.checked_neg())
    }

    fn checked_add(&mut self, addend_a: i64, addend_b: i64) -> Result<Option<i64>, ()> {
        Ok(addend_a.checked_add(addend_b))
    }
}

impl NestedRpc<()> for NestedRpcServerImpl {
    fn terminate(&mut self) -> Result<(), ()> {
        Err(())
    }
}

#[test]
fn nested_rpc_test() {
    let (server_transport, client_transport) = create_transport_from_pipes().unwrap();

    let handler: Box<dyn NestedRpcServer> = Box::new(NestedRpcServerImpl {});
    let mut dispatcher = RpcDispatcher::new_nonblocking(handler, server_transport).unwrap();

    // Queue the client RPC:
    let client_thread = spawn(move || {
        let mut rpc_client = NestedRpcClient::new(client_transport);

        let neg_resp = TestRpc::<rpc::Error>::checked_neg(&mut rpc_client, 125).unwrap();
        assert_matches!(neg_resp, Some(-125));

        let add_resp = OtherRpc::<rpc::Error>::checked_add(&mut rpc_client, 5, 4).unwrap();
        assert_matches!(add_resp, Some(9));

        assert!(rpc_client.terminate().is_err());
    });

    let sleep_for = None;
    assert_matches!(dispatcher.read_complete_message(sleep_for), Ok(None));
    assert_matches!(dispatcher.read_complete_message(sleep_for), Ok(None));
    assert_matches!(dispatcher.read_complete_message(sleep_for), Ok(Some(_)));
    // Explicitly call drop to close the pipe so the client thread gets the hang up since the return
    // value should be a RemoveFd mutator.
    drop(dispatcher);

    client_thread.join().unwrap();
}
