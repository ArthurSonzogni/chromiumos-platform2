// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod config;
mod cpu_utils;
pub mod dbus;
mod dbus_ownership_listener;
pub mod feature;
pub mod memory;
mod metrics;
mod power;
mod proc;
pub mod process_stats;
mod psi;
mod qos;
mod vm_concierge_client;
mod vm_memory_management_client;

#[cfg(test)]
mod test_utils;

#[cfg(target_arch = "x86_64")]
pub mod cgroup_x86_64;

#[cfg(target_arch = "x86_64")]
mod gpu_freq_scaling;

#[cfg(target_arch = "x86_64")]
mod cpu_scaling;

#[cfg(feature = "vm_grpc")]
mod vm_grpc;
