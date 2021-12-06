// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Verify sirenia-rpc_macros works for the intended use case.

extern crate sirenia_rpc_macros;

use std::thread::spawn;

use assert_matches::assert_matches;
use libsirenia::rpc::RpcDispatcher;
use libsirenia::transport::create_transport_from_pipes;
use sirenia_rpc_macros::sirenia_rpc;

#[sirenia_rpc]
pub trait TestRpc<E> {
    fn checked_neg(&mut self, input: i32) -> Result<Option<i32>, E>;
    fn checked_add(&mut self, addend_a: i32, addend_b: i32) -> Result<Option<i32>, E>;
    fn terminate(&mut self) -> Result<(), E>;
}

#[derive(Clone)]
struct TestRpcServerImpl {}

impl TestRpc<()> for TestRpcServerImpl {
    fn checked_neg(&mut self, input: i32) -> Result<Option<i32>, ()> {
        Ok(input.checked_neg())
    }

    fn checked_add(&mut self, addend_a: i32, addend_b: i32) -> Result<Option<i32>, ()> {
        Ok(addend_a.checked_add(addend_b))
    }

    fn terminate(&mut self) -> Result<(), ()> {
        Err(())
    }
}

#[test]
fn smoke_test() {
    let (server_transport, client_transport) = create_transport_from_pipes().unwrap();

    let handler: Box<dyn TestRpcServer> = Box::new(TestRpcServerImpl {});
    let mut dispatcher = RpcDispatcher::new_nonblocking(handler, server_transport).unwrap();

    // Queue the client RPC:
    let client_thread = spawn(move || {
        let mut rpc_client = TestRpcClient::new(client_transport);

        let neg_resp = rpc_client.checked_neg(125).unwrap();
        assert_matches!(neg_resp, Some(-125));

        let add_resp = rpc_client.checked_add(5, 4).unwrap();
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
