// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;
use std::sync::Mutex;

use anyhow::{bail, Context, Result};
use once_cell::sync::Lazy;

// Extract the parsing function for unittest.
pub fn parse_file_to_u64<R: BufRead>(reader: R) -> Result<u64> {
    let first_line = reader.lines().next().context("No content in buffer")??;
    first_line
        .parse()
        .with_context(|| format!("Couldn't parse \"{}\" as u64", first_line))
}

/// Get the first line in a file and parse as u64.
pub fn read_file_to_u64<P: AsRef<Path>>(filename: P) -> Result<u64> {
    let reader = File::open(filename).map(BufReader::new)?;
    parse_file_to_u64(reader)
}

#[derive(Clone, Copy, PartialEq)]
pub enum GameMode {
    // Game mode is off.
    Off = 0,
    // Game mode is on, borealis is the foreground subsystem.
    Borealis = 1,
}

static GAME_MODE: Lazy<Mutex<GameMode>> = Lazy::new(|| Mutex::new(GameMode::Off));

pub fn set_game_mode(mode: GameMode) -> Result<()> {
    match GAME_MODE.lock() {
        Ok(mut data) => {
            *data = mode;
            Ok(())
        }
        Err(_) => bail!("Failed to set game mode"),
    }
}

pub fn get_game_mode() -> Result<GameMode> {
    match GAME_MODE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get game mode"),
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum RTCAudioActive {
    // RTC is not active.
    Inactive = 0,
    // RTC is active, RTC audio is playing and recording.
    Active = 1,
}

static RTC_ACTIVE: Lazy<Mutex<RTCAudioActive>> = Lazy::new(|| Mutex::new(RTCAudioActive::Inactive));

pub fn set_rtc_audio_active(mode: RTCAudioActive) -> Result<()> {
    match RTC_ACTIVE.lock() {
        Ok(mut data) => {
            *data = mode;
            Ok(())
        }
        Err(_) => bail!("Failed to set RTC audio activity"),
    }
}

pub fn get_rtc_audio_active() -> Result<RTCAudioActive> {
    match RTC_ACTIVE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get status of RTC audio activity"),
    }
}
