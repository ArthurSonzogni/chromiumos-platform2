// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;

use lazy_static::lazy_static;

pub struct RtcFsSignal {
    signal: AtomicBool,
}
//global varibale to turn dynamic EPP on/off
pub struct DynamicEpp {
    signal: AtomicBool,
}

pub struct MediaCgroup {
    signal: AtomicBool,
}

pub struct BsmSignal {
    signal: AtomicBool,
}

impl RtcFsSignal {
    pub fn new(initial_value: bool) -> Self {
        Self {
            signal: AtomicBool::new(initial_value),
        }
    }

    pub fn set_signal(&self, value: bool) {
        self.signal.store(value, Ordering::SeqCst);
    }

    pub fn read_signal(&self) -> bool {
        self.signal.load(Ordering::SeqCst)
    }
}

impl DynamicEpp {
    pub fn new(initial_value: bool) -> Self {
        Self {
            signal: AtomicBool::new(initial_value),
        }
    }

    pub fn set_value(&self, value: bool) {
        self.signal.store(value, Ordering::SeqCst);
    }

    pub fn read_value(&self) -> bool {
        self.signal.load(Ordering::SeqCst)
    }
}

impl BsmSignal {
    pub fn new(initial_value: bool) -> Self {
        Self {
            signal: AtomicBool::new(initial_value),
        }
    }
    pub fn set_signal(&self, value: bool) {
        self.signal.store(value, Ordering::SeqCst);
    }
    pub fn read_signal(&self) -> bool {
        self.signal.load(Ordering::SeqCst)
    }
}

impl MediaCgroup {
    pub fn new(initial_value: bool) -> Self {
        Self {
            signal: AtomicBool::new(initial_value),
        }
    }
    pub fn set_signal(&self, value: bool) {
        self.signal.store(value, Ordering::SeqCst);
    }
    pub fn read_signal(&self) -> bool {
        self.signal.load(Ordering::SeqCst)
    }
}

lazy_static! {
    pub static ref RTC_FS_SIGNAL: RtcFsSignal = RtcFsSignal::new(false);
}

lazy_static! {
    pub static ref DYNAMIC_EPP: DynamicEpp = DynamicEpp::new(false);
}

lazy_static! {
    pub static ref BSM: BsmSignal = BsmSignal::new(false);
}

lazy_static! {
    pub static ref AUTOEPP_RUNNING: AtomicBool = AtomicBool::new(false);
}

lazy_static! {
    pub static ref MEDIA_CGROUP: MediaCgroup = MediaCgroup::new(false);
}

pub fn set_rtc_fs_signal_state(value: bool) {
    RTC_FS_SIGNAL.set_signal(value);
}

// Function to read RTC_FS_SIGNAL_STATE
pub fn read_rtc_fs_signal_state() -> bool {
    RTC_FS_SIGNAL.read_signal()
}

//Dynamic EPP feature value
pub fn set_dynamic_epp_feature(value: bool) {
    DYNAMIC_EPP.set_value(value);
}

pub fn read_dynamic_epp_feature() -> bool {
    DYNAMIC_EPP.read_value()
}

pub fn set_bsm_signal_state(value: bool) {
    BSM.set_signal(value);
}

// Function to read BSM_SIGNAL
pub fn read_bsm_signal_state() -> bool {
    BSM.read_signal()
}

pub fn set_media_cgroup_state(value: bool) {
    MEDIA_CGROUP.set_signal(value);
}

// Function to read MediaCgroup_SIGNAL
pub fn read_media_cgroup_state() -> bool {
    MEDIA_CGROUP.read_signal()
}

pub fn set_autoepp_running_status(new_status: bool) {
    AUTOEPP_RUNNING.store(new_status, Ordering::SeqCst);
}
