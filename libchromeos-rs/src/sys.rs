// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Re-export types from crosvm_base that are used in CrOS.
// Note: This list is supposed to shrink over time as crosvm_base functionality is replaced with
// third_party crates or ChromeOS specific implementations.
// Do not add to this list.
pub use crosvm_base::block_signal;
pub use crosvm_base::errno_result;
pub use crosvm_base::set_rt_prio_limit;
pub use crosvm_base::set_rt_round_robin;
pub use crosvm_base::signal::Error as SignalError;
pub use crosvm_base::unblock_signal;
pub use crosvm_base::unix::clear_signal_handler;
pub use crosvm_base::unix::duration_to_timespec;
pub use crosvm_base::unix::panic_handler;
pub use crosvm_base::unix::register_signal_handler;
pub use crosvm_base::unix::vsock;
pub use crosvm_base::unix::SharedMemory;
pub use crosvm_base::AsRawDescriptor;
pub use crosvm_base::Error;
pub use crosvm_base::FromRawDescriptor;
pub use crosvm_base::IntoRawDescriptor;
pub use crosvm_base::RawDescriptor;
pub use crosvm_base::Result;
pub use crosvm_base::SafeDescriptor;
pub use crosvm_base::ScmSocket;

pub mod net {
    // TODO: b/293488266 - still used by libcras, remove after refactoring.
    pub use crosvm_base::net::UnixSeqpacket;
}
