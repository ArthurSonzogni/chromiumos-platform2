// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;

macro_rules! global_flag {
    ($type_name:ident, $flag_name:ident) => {
        pub struct $type_name {
            value: AtomicBool,
        }

        pub static $flag_name: $type_name = $type_name {
            value: AtomicBool::new(false),
        };

        impl $type_name {
            pub fn set_value(&self, value: bool) {
                self.value.store(value, Ordering::SeqCst);
            }

            pub fn read_value(&self) -> bool {
                self.value.load(Ordering::SeqCst)
            }
        }
    };
}

// Flag indicating RTC audio/full screen video is active
global_flag!(RtcFsSignal, RTC_FS_SIGNAL);
// Flag indicating Media Cgroups are active
global_flag!(MediaCgroupSignal, MEDIA_CGROUP_SIGNAL);
// Flag indicating BSM is active
global_flag!(BsmSignal, BSM_SIGNAL);

// Global variable indicating whether dynamic EPP is enabled/disabled
global_flag!(DynamicEpp, DYNAMIC_EPP);
