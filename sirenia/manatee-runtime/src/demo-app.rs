// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Demo application for use with the sirenia tast test.

use std::borrow::{Borrow, BorrowMut};
use std::io;

use manatee_runtime::storage::TrichechusStorage;
use manatee_runtime::{ExclusiveScopedData, ScopedData};
use sys_util::{info, syslog};

/* For this example test a blank string should be returned when the id is not
 * found in the backing store.
 */
fn callback_id_not_found(_s: &str) -> String {
    "".to_string()
}

// A test demo app that just stores a value that is written to stdin then
// rereads it out of storage and prints back out to stdout.
fn main() {
    if let Err(e) = syslog::init() {
        eprintln!("failed to initialize syslog: {}", e);
        return;
    }

    info!("Starting up storage");
    let mut store = TrichechusStorage::new();

    {
        let mut buffer = String::new();
        info!("Creating scoped data");
        let mut data: ExclusiveScopedData<String, TrichechusStorage> =
            ScopedData::new(&mut store, "Test id", callback_id_not_found).unwrap();
        let s: &mut String = data.borrow_mut();
        info!("Reading data");
        match io::stdin().read_line(&mut buffer) {
            Ok(_) => {
                s.push_str(&buffer);
            }
            Err(error) => println!("error: {}", error),
        }
    }

    let data2: ExclusiveScopedData<String, TrichechusStorage> =
        ScopedData::new(&mut store, "Test id", callback_id_not_found).unwrap();
    let s2: &String = data2.borrow();
    print!("{}", s2);
}
