// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for working with signal handlers
use libc::c_int;
use nix::sys::signal::pthread_sigmask;
use nix::sys::signal::sigaction;
use nix::sys::signal::SaFlags;
use nix::sys::signal::SigAction;
use nix::sys::signal::SigHandler;
use nix::sys::signal::SigSet;
use nix::sys::signal::SigmaskHow;
use nix::sys::signal::Signal;

/// Registers `handler` as the signal handler of signum `num`.
///
/// # Safety
///
/// This is considered unsafe because the given handler will be called asynchronously, interrupting
/// whatever the thread was doing and therefore must only do async-signal-safe operations.
pub unsafe fn register_signal_handler(
    num: Signal,
    handler: extern "C" fn(c_int),
) -> nix::Result<()> {
    sigaction(
        num,
        &SigAction::new(
            SigHandler::Handler(handler),
            SaFlags::SA_RESTART,
            SigSet::empty(),
        ),
    )
    .map(|_| ())
}

/// Resets the signal handler of signum `num` back to the default.
pub fn clear_signal_handler(num: Signal) -> nix::Result<()> {
    // SAFETY: Reverting to SigDfl is safe, we are not setting a handler.
    unsafe {
        sigaction(
            num,
            &SigAction::new(SigHandler::SigDfl, SaFlags::SA_RESTART, SigSet::empty()),
        )
        .map(|_| ())
    }
}

/// Masks given signal.
pub fn block_signal(num: Signal) -> nix::Result<()> {
    let mut sigset = SigSet::empty();
    sigset.add(num);
    pthread_sigmask(SigmaskHow::SIG_BLOCK, Some(&sigset), None)
}

/// Unmasks given signal.
pub fn unblock_signal(num: Signal) -> nix::Result<()> {
    let mut sigset = SigSet::empty();
    sigset.add(num);
    pthread_sigmask(SigmaskHow::SIG_UNBLOCK, Some(&sigset), None)
}
