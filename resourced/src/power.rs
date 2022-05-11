// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::{Context, Result};

use crate::common;
use crate::config;

const POWER_SUPPLY_PATH: &str = "sys/class/power_supply";
const POWER_SUPPLY_ONLINE: &str = "online";

pub trait PowerSourceProvider {
    /// Returns the current power source of the system.
    fn get_power_source(&self) -> Result<config::PowerSourceType>;
}

#[derive(Debug)]
pub struct DirectoryPowerSourceProvider<'a> {
    pub root: &'a Path,
}

impl<'a> PowerSourceProvider for DirectoryPowerSourceProvider<'a> {
    /// Iterates through all the power supplies in sysfs and looks for the `online` property.
    /// If online == 1, then the system is connected to external power (AC), otherwise the
    /// system is powered via battery (DC).
    fn get_power_source(&self) -> Result<config::PowerSourceType> {
        let path = self.root.join(POWER_SUPPLY_PATH);

        if !path.exists() {
            return Ok(config::PowerSourceType::DC);
        }

        let dirs = path
            .read_dir()
            .with_context(|| format!("Failed to enumerate power supplies in {}", path.display()))?;

        for result in dirs {
            let charger_path = result?;

            let online_path = charger_path.path().join(POWER_SUPPLY_ONLINE);

            if !online_path.exists() {
                continue;
            }

            let online = common::read_file_to_u64(&online_path)
                .with_context(|| format!("Error reading online from {}", online_path.display()))?
                as u32;

            if online == 1 {
                return Ok(config::PowerSourceType::AC);
            }
        }

        Ok(config::PowerSourceType::DC)
    }
}
