// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common;
use crate::vm_grpc::proto::resourced_bridge::BatteryData_DNotifierPowerState;
use anyhow::{bail, Result};
use std::{fs, path::PathBuf};

const DEVICE_BATTERY_PATH: &str = "sys/class/power_supply/BAT0";

pub struct DeviceBatteryStatus {
    charging_status_path: PathBuf,
    battery_charge_full_path: PathBuf,
    battery_charge_now_path: PathBuf,
}

#[allow(dead_code)]
impl DeviceBatteryStatus {
    fn get_sysfs_val(&self, path_buf: &PathBuf) -> Result<u64> {
        Ok(common::read_file_to_u64(path_buf.as_path())? as u64)
    }

    fn get_sysfs_string(&self, path_buf: &PathBuf) -> Result<String> {
        if let Ok(mut sysfs_contents) = fs::read_to_string(path_buf) {
            sysfs_contents = sysfs_contents.trim_end().replace('\n', "");

            Ok(sysfs_contents)
        } else {
            bail!("could not read sysfs file: {:?}", path_buf);
        }
    }

    // Boolean denoting if device is currently charing.  Used for sending gRPC on AC/DC switch.
    pub fn is_charging(&self) -> Result<bool> {
        // Battery status can be Full, Charging or Discharging.
        // We clump Full with Charging when checking.
        Ok(self.get_sysfs_string(&self.charging_status_path)? != "Discharging")
    }

    // Current battery percent value.
    pub fn get_percent(&self) -> Result<f32> {
        let bmax = self.get_sysfs_val(&self.battery_charge_full_path)? as f32;
        let bcurr = self.get_sysfs_val(&self.battery_charge_now_path)? as f32;

        if bmax == 0.0 {
            bail!("Battery charge full read as 0.");
        }
        Ok(bcurr / bmax * 100.0)
    }

    // Create a new battry helper object.  Object is isolated functionally from those in
    // `power.rs` since many of the functionality is vendor specific.
    pub fn new(root: PathBuf) -> Result<DeviceBatteryStatus> {
        let charging_status_path = root.join(DEVICE_BATTERY_PATH).join("status");
        let battery_charge_full_path = root.join(DEVICE_BATTERY_PATH).join("charge_full");
        let battery_charge_now_path = root.join(DEVICE_BATTERY_PATH).join("charge_now");

        if charging_status_path.exists()
            && battery_charge_full_path.exists()
            && battery_charge_now_path.exists()
        {
            Ok(DeviceBatteryStatus {
                charging_status_path,
                battery_charge_full_path,
                battery_charge_now_path,
            })
        } else {
            bail!("Could not find all sysfs files for battery status");
        }
    }

    // Placeholder for notifier level.  This may be deprecated in favor of ACPI interrupts
    // containing the DNOTIFER level.
    pub fn get_notifier_level(batt_percent: f32) -> Result<BatteryData_DNotifierPowerState> {
        match batt_percent {
            x if (0.0..60.0).contains(&x) => {
                Ok(BatteryData_DNotifierPowerState::DNOTIFIER_POWER_STATE_D5)
            }
            x if (60.0..70.0).contains(&x) => {
                Ok(BatteryData_DNotifierPowerState::DNOTIFIER_POWER_STATE_D4)
            }
            x if (70.0..80.0).contains(&x) => {
                Ok(BatteryData_DNotifierPowerState::DNOTIFIER_POWER_STATE_D3)
            }
            x if (80.0..90.0).contains(&x) => {
                Ok(BatteryData_DNotifierPowerState::DNOTIFIER_POWER_STATE_D2)
            }
            x if (90.0..=100.0).contains(&x) => {
                Ok(BatteryData_DNotifierPowerState::DNOTIFIER_POWER_STATE_D1)
            }
            _ => bail!("Battery percent outside range 0 to 100"),
        }
    }
}
