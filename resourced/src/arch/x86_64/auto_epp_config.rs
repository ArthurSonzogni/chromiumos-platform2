// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::arch::x86_64::__cpuid;
use std::time::Duration;

use lazy_static::lazy_static;

// Feature flag
pub const PREVENT_OVERTURBO: bool = true;

// Track Max consecutive errors while parsing cpu stats
pub const MAX_CONSECUTIVE_ERRORS: u32 = 100;

// CPU model constants
// for ADL set MTL to 0x906 to enable this feature
const MTL: u32 = 0xA06;

// CPUID constants
const CPUID_EAX_VERSION_INFO: u32 = 0x0000_0001;

// IS_MTL variable is used to check for MTL SOCs.
// If dynamic EPP needs to be enabled for other SOCs,
// define corresponding varible to check for those model numbers
// and add it to the epp, thresholds, timeconstants list below.
lazy_static! {
    pub static ref IS_MTL: bool = {
        // Check if it's an Intel platform
        let is_intel_platform = is_intel_platform().unwrap_or(false);
        // Check for a specific Intel CPU model
        let version_info = unsafe { __cpuid(CPUID_EAX_VERSION_INFO) };
        let model_info = (version_info.eax >> 8) & 0xFFF;


        let is_specific_model = model_info == MTL;

        is_intel_platform && is_specific_model
    };
}

fn is_intel_platform() -> Result<bool, String> {
    const CPUID_EAX_FOR_HFP_MID: u32 = 0;
    const CPUID_GENUINE_INTEL_EBX: u32 = 0x756e6547;
    const CPUID_GENUINE_INTEL_ECX: u32 = 0x6c65746e;
    const CPUID_GENUINE_INTEL_EDX: u32 = 0x49656e69;
    const CPUID_EAX_EXT_FEATURE: u32 = 7;

    let result = unsafe { __cpuid(CPUID_EAX_FOR_HFP_MID) };

    if result.eax >= CPUID_EAX_EXT_FEATURE
        && result.ebx == CPUID_GENUINE_INTEL_EBX
        && result.ecx == CPUID_GENUINE_INTEL_ECX
        && result.edx == CPUID_GENUINE_INTEL_EDX
    {
        Ok(true)
    } else {
        Ok(false)
    }
}

// Configure EPP constants for differnt SOCs/Platforms
// epp_perf: Epp setting for performance mode
// epp_allcore: Epp setting when all cores are busrting at same time
// epp_bal: Epp setting for balance performance mode
// epp_eff: Epp setting for efficiency mode
pub struct Epp;

impl Epp {
    pub fn epp_perf() -> u32 {
        if *IS_MTL {
            64
        } else {
            84
        }
    }

    pub fn epp_allcore() -> u32 {
        128
    }

    pub fn epp_bal() -> u32 {
        128
    }

    pub fn epp_eff() -> u32 {
        if *IS_MTL {
            192
        } else {
            128
        }
    }

    pub fn epp_default() -> u32 {
        if *IS_MTL {
            115
        } else {
            128
        }
    }
}

// Configure Thresholds for differnt SOCs/Platforms
// Thresholds are generally in the range 0% - 100%
// rtc_fs_high: Threshold high for RTC and Full screeen mode
// rtc_fs_mod: Threshold moderate for RTC and Full screeen mode
// normal_high: Threshold high for regular/normal mode of operation
// normal_mod: Threshold moderate for regular/normal mode of operation
// gpu_rc6: Threshold for GPU RC6 residency
// prevent_overturbo: Threshold for  50% core burst at the same time
pub struct ThresholdsIntel;

impl ThresholdsIntel {
    pub fn rtc_fs_high() -> f64 {
        if *IS_MTL {
            85.0
        } else {
            90.0
        }
    }

    pub fn rtc_fs_mod() -> f64 {
        if *IS_MTL {
            65.0
        } else {
            80.0
        }
    }

    pub fn normal_high() -> f64 {
        if *IS_MTL {
            55.0
        } else {
            60.0
        }
    }

    pub fn normal_mod() -> f64 {
        if *IS_MTL {
            30.0
        } else {
            40.0
        }
    }

    pub fn gpu_rc6() -> f64 {
        if *IS_MTL {
            40.0
        } else {
            50.0
        }
    }

    pub fn prevent_overturbo() -> f64 {
        if *IS_MTL {
            60.0
        } else {
            50.0
        }
    }
}

// Configure Time constants for differnt SOCs/Platforms
// cpu_sampling_time: Sampling time for auto_epp_main funciton
// high_threshold_time: Time duration for which performance
//                      based EPP is applied to the system
// low_threshold_time: Time duration for which all cores are
//                      below moderate threshold in order to
//                      apply efficiency epp
// alpha: Weight used by EMA(Exponential Moving Average).
//        alpha is between 0 and 1
// gpu_max_residency: Time interval to sample RC6 residency
pub struct TimeConstantsIntel;

impl TimeConstantsIntel {
    pub fn cpu_sampling_time() -> Duration {
        if *IS_MTL {
            Duration::from_millis(100)
        } else {
            Duration::from_millis(250)
        }
    }

    pub fn high_threshold_time() -> Duration {
        if *IS_MTL {
            Duration::from_secs(5)
        } else {
            Duration::from_secs(8)
        }
    }

    pub fn low_threshold_time() -> Duration {
        if *IS_MTL {
            Duration::from_secs(10)
        } else {
            Duration::from_secs(4)
        }
    }

    pub fn alpha() -> f64 {
        0.8
    }

    pub fn gpu_max_residency() -> Duration {
        if *IS_MTL {
            Duration::from_millis(35)
        } else {
            Duration::from_millis(100)
        }
    }
}
