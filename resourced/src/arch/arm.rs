// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::Result;

use crate::common::BatterySaverMode;
use crate::common::FullscreenVideo;
use crate::common::RTCAudioActive;
use crate::common::TuneSwappiness;
use crate::config::PowerPreferences;
use crate::config::PowerSourceType;

pub fn init() {}

pub fn apply_platform_power_settings(
    _root_path: &Path,
    _power_source: PowerSourceType,
    _rtc: RTCAudioActive,
    _fullscreen: FullscreenVideo,
    _bsm: BatterySaverMode,
    _need_default_epp: bool,
) -> Result<()> {
    Ok(())
}

pub fn apply_platform_power_preferences(
    _root_path: &Path,
    _preferences: &PowerPreferences,
) -> Result<()> {
    Ok(())
}

pub fn apply_borealis_tuning(_root: &Path, _enable: bool) -> Option<TuneSwappiness> {
    None
}
