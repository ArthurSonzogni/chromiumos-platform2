// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cfg_if::cfg_if! {
    if #[cfg(target_arch = "x86_64")] {
        mod x86_64;

        use x86_64 as arch_impl;
    } else if #[cfg(any(target_arch = "arm", target_arch = "aarch64"))] {
        mod arm;

        use arm as arch_impl;
    }
}

pub use arch_impl::apply_borealis_tuning;
pub use arch_impl::apply_platform_power_preferences;
pub use arch_impl::apply_platform_power_settings;
pub use arch_impl::init;
