// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Mutex;
use std::sync::MutexGuard;

/// Resourced is compiled to abort on panic, so it is impossible for Mutexes
/// to be poisoned. This is a helper trait to centralize discard the LockResult,
/// without needing to unwrap()/expect() everywhere a lock is used.
pub trait NoPoison<T: ?Sized> {
    fn do_lock(&self) -> MutexGuard<T>;
}

impl<T: ?Sized> NoPoison<T> for Mutex<T> {
    fn do_lock(&self) -> MutexGuard<T> {
        match self.lock() {
            Ok(guard) => guard,
            Err(_) => unreachable!("resourced aborts on panic"),
        }
    }
}
