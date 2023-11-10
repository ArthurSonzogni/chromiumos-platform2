// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use regex::Regex;
use std::{collections::HashMap, path::PathBuf, time::Duration};

pub type SteamAppId = u64;

#[cfg(not(test))]
pub const UNMOUNTER_INTERVAL: Duration = Duration::from_millis(1000);
#[cfg(test)]
pub const UNMOUNTER_INTERVAL: Duration = Duration::from_millis(10);

pub const DLC_HANDLER_INTERVAL: Duration = Duration::from_millis(1000);
pub const MAX_CONCURRENT_DLC_INSTALLS: usize = 1;
// Limit the number of DLC installation queue to:
// - Prevent 'hogging' DlcService. DlcService does not support parallel
//   downloads or queues yet.
// - Prevent the user from increasing shadercached memory footprint infinitely.
//   VM user can queue shader cache DLC installations by design.
pub const MAX_INSTALL_QUEUE_SIZE: usize = 5;

pub const PRECOMPILED_CACHE_DIR: &str = "precompiled_cache";

// GPU device id reported by the pcie ID
#[cfg(test)]
lazy_static! {
    pub static ref IMAGE_LOADER: PathBuf = tempfile::tempdir().unwrap().into_path();
    pub static ref CRYPTO_HOME: PathBuf = tempfile::tempdir().unwrap().into_path();
    pub static ref GPU_DEVICE_ID: u16 = 0x9a40;
}

#[cfg(not(test))]
lazy_static! {
    pub static ref IMAGE_LOADER: PathBuf = std::path::Path::new("/run/imageloader").to_path_buf();
    pub static ref CRYPTO_HOME: PathBuf =
        std::path::Path::new("/run/daemon-store/shadercached").to_path_buf();
    pub static ref GPU_DEVICE_ID: u16 = get_gpu_device_id().unwrap_or(0);
}

lazy_static! {
    pub static ref GPU_DEVICE_DLC_VARIANT: &'static str = {
        // These suffixes are non-technical names to create buckets for each
        // device id variants per board.
        const DLC_VARIANT_AXE: &str = "-axe";
        const DLC_VARIANT_BATRIDER: &str = "-batrider";
        const DLC_VARIANT_CLINKZ: &str = "-clinkz";
        const DLC_VARIANT_DAZZLE: &str = "-dazzle";
        const DLC_VARIANT_ENIGMA: &str = "-enigma";
        const DLC_VARIANT_GRIMSTROKE: &str = "-grimstroke";

        let variant_mapping: HashMap<u16, &str> = HashMap::from([
            // axe variant
            (u16::from_str_radix("9a49", 16).unwrap(), DLC_VARIANT_AXE),  // volteer
            (u16::from_str_radix("46a6", 16).unwrap(), DLC_VARIANT_AXE),  // brya
            (u16::from_str_radix("46d0", 16).unwrap(), DLC_VARIANT_AXE),  // nissa
            (u16::from_str_radix("9b41", 16).unwrap(), DLC_VARIANT_AXE),  // hatch

            // batrider variant
            (u16::from_str_radix("9a40", 16).unwrap(), DLC_VARIANT_BATRIDER),  // volteer
            (u16::from_str_radix("46b3", 16).unwrap(), DLC_VARIANT_BATRIDER),  // brya
            (u16::from_str_radix("9bcc", 16).unwrap(), DLC_VARIANT_BATRIDER),  // hatch

            // clinkz variant
            (u16::from_str_radix("9a78", 16).unwrap(), DLC_VARIANT_CLINKZ),  // volteer
            (u16::from_str_radix("a7a0", 16).unwrap(), DLC_VARIANT_CLINKZ),  // brya
            (u16::from_str_radix("9bca", 16).unwrap(), DLC_VARIANT_CLINKZ),  // hatch

            // dazzle variant
            (u16::from_str_radix("a7a9", 16).unwrap(), DLC_VARIANT_DAZZLE),  // brya
            (u16::from_str_radix("9bac", 16).unwrap(), DLC_VARIANT_DAZZLE),  // hatch

            // enigma variant
            (u16::from_str_radix("a7a1", 16).unwrap(), DLC_VARIANT_ENIGMA),  // brya
            (u16::from_str_radix("9b21", 16).unwrap(), DLC_VARIANT_ENIGMA),  // hatch

            // grimstroke variant
            (u16::from_str_radix("46a8", 16).unwrap(), DLC_VARIANT_GRIMSTROKE),  // brya
        ]);

        // If no device id is detected or not found in |variant_mapping|,
        // shadercached should attempt to install axe variant.
        variant_mapping.get(&*GPU_DEVICE_ID).unwrap_or(&DLC_VARIANT_AXE)
    };

    pub static ref BOOT_ID: String = {
        const BOOT_ID_FILE: &str = "/proc/sys/kernel/random/boot_id";
        let contents = std::fs::read(BOOT_ID_FILE).expect(
            "Expected to be able to read the boot id file");
        hex::encode(openssl::sha::sha256(&contents))
    };

    pub static ref OS_BUILD_ID: String = {
        if let Ok(os_release) = sys_info::linux_os_release() {
            if let Some(build_id) = os_release.build_id {
                return hex::encode(openssl::sha::sha256(build_id.as_bytes()))
            }
        }

        // Fall back to boot id
        BOOT_ID.to_string()
    };
}

pub fn steam_app_id_to_dlc(steam_app_id: SteamAppId) -> String {
    format!(
        "borealis-shader-cache-{}-dlc{}",
        steam_app_id, *GPU_DEVICE_DLC_VARIANT
    )
}

pub fn dlc_to_steam_app_id(dlc_name: &str) -> Result<SteamAppId> {
    lazy_static! {
        static ref RE: Regex = Regex::new(r"borealis-shader-cache-([0-9]+)-dlc(-.+)?").unwrap();
    }
    if let Some(capture) = RE.captures(dlc_name) {
        if let Some(steam_app_id_match) = capture.get(1) {
            return steam_app_id_match
                .as_str()
                .parse::<SteamAppId>()
                .map_err(|e| anyhow!(e));
        }
    }
    Err(anyhow!("Not a valid DLC"))
}

#[cfg(not(test))]
fn get_gpu_device_id() -> Result<u16> {
    use log::warn;
    use std::process::{Command, Stdio};
    // This function is called only once to initialize pub lazy static constant
    // GPU_DEVICE_ID, so we don't need to make the Regex object static.
    let regex = Regex::new(r"\[([0-9a-f]{4})\]").unwrap();
    let output = Command::new("lspci")
        .args(["-nn", "-d", "::0300", "-mm"]) // -d ::0300 returns only VGA device
        .stdout(Stdio::piped())
        .output()?;
    let vga_pcie_info = String::from_utf8(output.stdout)?;

    // Match the regex pattern for all occurrences
    let mut all_captures = regex.captures_iter(&vga_pcie_info);
    // Get the 3rd match, which has the GPU PCIE device ID
    if let Some(capture) = all_captures.nth(2) {
        // For the capture (ex. [abcd]), get the first inner match
        if let Some(device_id_match) = capture.get(1) {
            if let Ok(id) = u16::from_str_radix(device_id_match.as_str(), 16) {
                return Ok(id);
            } else {
                warn!("Failed to parse device ID: {}", device_id_match.as_str());
            }
        } else {
            warn!("Unable to extract PCI device ID, {}", vga_pcie_info);
        }
    } else {
        warn!("Unexpected VGA PCI information, {}", vga_pcie_info);
    }

    Err(anyhow!("Unable to determine PCI device ID!"))
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_steam_app_id_to_dlc() {
        assert_eq!(
            super::steam_app_id_to_dlc(32),
            "borealis-shader-cache-32-dlc-batrider"
        );
        assert_eq!(
            super::steam_app_id_to_dlc(123),
            "borealis-shader-cache-123-dlc-batrider"
        );
        assert_eq!(
            super::steam_app_id_to_dlc(0000),
            "borealis-shader-cache-0-dlc-batrider"
        );
    }

    #[test]
    fn test_dlc_to_steam_app_id() {
        assert_eq!(
            super::dlc_to_steam_app_id("borealis-shader-cache-32-dlc-axe").unwrap(),
            32
        );
        assert_eq!(
            super::dlc_to_steam_app_id("borealis-shader-cache-000-dlc-batrider").unwrap(),
            0
        );
        assert_eq!(
            super::dlc_to_steam_app_id("borealis-shader-cache-000-dlc").unwrap(),
            0
        );
        assert!(super::dlc_to_steam_app_id("borealis-shader-cache-213").is_err());
        assert!(super::dlc_to_steam_app_id("213-dlc").is_err());
        assert!(super::dlc_to_steam_app_id("not-a-valid-one").is_err());
        assert!(super::dlc_to_steam_app_id("borealis-dlc").is_err());
        assert!(super::dlc_to_steam_app_id("borealis-shader-cache-two-dlc").is_err());
    }
}
