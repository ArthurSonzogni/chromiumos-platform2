// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::arch::x86_64::CpuidResult;
use std::arch::x86_64::__cpuid;
use std::arch::x86_64::__cpuid_count;
use std::fs::read_to_string;
use std::path::Path;
use std::path::PathBuf;

use anyhow::Result;
use glob::glob;
use lazy_static::lazy_static;

// CPU model constants
const TGL: u32 = 0x806;
const MTL: u32 = 0xA06;

// CPUID constants
const CPUID_EAX_VERSION_INFO: u32 = 0x0000_0001;

fn is_cpu_model(model: u32) -> bool {
    // Check if it's an Intel platform
    let is_intel_platform = is_intel_platform().unwrap_or(false);
    // Check for a specific Intel CPU model
    let version_info = unsafe { __cpuid(CPUID_EAX_VERSION_INFO) };
    let model_info = (version_info.eax >> 8) & 0xFFF;

    let is_specific_model = model_info == model;

    is_intel_platform && is_specific_model
}

// Finds the sysfs path of the TBT controllers if exist
fn find_tbt_controllers(root: &Path) -> Option<Vec<PathBuf>> {
    // The TBT controllers list contains the PCI device ID which causes system hang
    // while CPU offline.
    let tbt_controllers = ["0x9a23", "0x9a25", "0x9a27", "0x9a29"];
    let mut found_tbt: Vec<PathBuf> = Vec::new();

    let pattern = root
        .join("sys/devices/pci*/*")
        .to_str()
        .expect("Failed to construct sys/devices/pci*/* glob pattern")
        .to_owned();

    if let Ok(pci_paths) = glob(&pattern) {
        for entry in pci_paths.flatten() {
            match read_to_string(entry.join("device")) {
                Ok(pci_id) => {
                    if tbt_controllers.contains(&pci_id.trim()) {
                        found_tbt.push(entry);
                    }
                }
                Err(_) => continue,
            }
        }
    }

    if found_tbt.is_empty() {
        None
    } else {
        Some(found_tbt)
    }
}

lazy_static! {
    pub static ref IS_TGL: bool = is_cpu_model(TGL);
    pub static ref IS_MTL: bool = is_cpu_model(MTL);
    pub static ref TBT_CONTROLLERS: Option<Vec<PathBuf>> = find_tbt_controllers(Path::new("/"));
}

// Check if the platform is Intel
pub fn is_intel_platform() -> Result<bool> {
    // cpuid with EAX=0: Highest Function Parameter and Manufacturer ID.
    // This returns the CPU's manufacturer ID string.
    // The largest value that EAX can be set to before calling CPUID is returned in EAX.
    // https://en.wikipedia.org/wiki/CPUID.
    const CPUID_EAX_FOR_HFP_MID: u32 = 0;

    // Intel processor manufacture ID is "GenuineIntel".
    const CPUID_GENUINE_INTEL_EBX: u32 = 0x756e6547;
    const CPUID_GENUINE_INTEL_ECX: u32 = 0x6c65746e;
    const CPUID_GENUINE_INTEL_EDX: u32 = 0x49656e69;

    // ChromeOS does't expect to run on anything without the extended feature
    // flag cpuid leaf, so add this check here for a sanity check.
    const CPUID_EAX_EXT_FEATURE: u32 = 7;

    // Check manufacturer ID is "GenuineIntel" and the highest function supports extended features.
    match unsafe { __cpuid(CPUID_EAX_FOR_HFP_MID) } {
        CpuidResult {
            eax,
            ebx: CPUID_GENUINE_INTEL_EBX,
            ecx: CPUID_GENUINE_INTEL_ECX,
            edx: CPUID_GENUINE_INTEL_EDX,
        } if eax >= CPUID_EAX_EXT_FEATURE => Ok(true),
        _ => Ok(false),
    }
}

// Check Intel hybrid platform.
pub fn is_intel_hybrid_platform() -> Result<bool> {
    if !is_intel_platform()? {
        return Ok(false);
    }

    // cpuid with EAX=7 and ECX=0: Extended Features.
    // 15th bit in EDX tells platform has hybrid architecture or not.
    const CPUID_EAX_EXT_FEATURE: u32 = 7;
    const CPUID_ECX_EXT_FEATURE: u32 = 0;
    const CPUID_EDX_HYBRID_SHIFT: u32 = 15;

    // Read hybrid information.
    let hybrid_info = unsafe { __cpuid_count(CPUID_EAX_EXT_FEATURE, CPUID_ECX_EXT_FEATURE) };

    // Check system has Intel hybrid architecture.
    Ok(hybrid_info.edx & (1 << CPUID_EDX_HYBRID_SHIFT) > 0)
}

#[cfg(test)]
mod tests {
    use std::fs::create_dir_all;
    use std::fs::write;

    use tempfile::tempdir;

    use super::*;

    #[test]
    fn test_is_intel_hybrid_system() {
        // cpuid with EAX=0: Highest Function Parameter and Manufacturer ID.
        // This returns the CPU's manufacturer ID string.
        // The largest value that EAX can be set to before calling CPUID is returned in EAX.
        // https://en.wikipedia.org/wiki/CPUID.
        const CPUID_EAX_FOR_HFP_MID: u32 = 0;

        // Intel processor manufacture ID is "GenuineIntel".
        const CPUID_GENUINE_INTEL_EBX: u32 = 0x756e6547;
        const CPUID_GENUINE_INTEL_ECX: u32 = 0x6c65746e;
        const CPUID_GENUINE_INTEL_EDX: u32 = 0x49656e69;
        const CPUID_EAX_EXT_FEATURE: u32 = 7;

        // Check system has Intel platform i.e "GenuineIntel" and the highest function.
        let (intel_platform, highest_feature) = match unsafe { __cpuid(CPUID_EAX_FOR_HFP_MID) } {
            CpuidResult {
                eax,
                ebx: CPUID_GENUINE_INTEL_EBX,
                ecx: CPUID_GENUINE_INTEL_ECX,
                edx: CPUID_GENUINE_INTEL_EDX,
            } => {
                println!("Intel platform with highest function: {eax}");
                (true, eax)
            }
            _ => (false, 0),
        };

        let intel_hybrid_platform = is_intel_hybrid_platform().unwrap();

        // If system is not Intel platform, hybrid should be false.
        if !intel_platform {
            assert!(!intel_hybrid_platform);
        }

        // If system is Intel platform but if the highest function is less than 7
        // hybrid should be false.
        if intel_platform && highest_feature < CPUID_EAX_EXT_FEATURE {
            assert!(!intel_hybrid_platform);
        }

        println!("Does platform support Intel hybrid feature? {intel_hybrid_platform}");
    }

    #[test]
    fn test_find_tbt_controllers() {
        let dir = tempdir().unwrap();
        let root = dir.path();

        // Create a mock PCI device directory but don't have the "device" file
        let device_dir = root.join("sys/devices/pci0000:00/0000:00:00.0");
        create_dir_all(&device_dir).unwrap();
        let results = find_tbt_controllers(root);
        assert_eq!(results, None);

        // Create a 0x9a23 TBT controller
        write(device_dir.join("device"), "0x9a23\n").unwrap();
        let results = find_tbt_controllers(root);
        assert_eq!(results, Some(vec![device_dir.clone()]));

        // Create a 0x9a25 TBT controller
        write(device_dir.join("device"), "0x9a25\n").unwrap();
        let results = find_tbt_controllers(root);
        assert_eq!(results, Some(vec![device_dir.clone()]));

        // Create a 0x9a27 TBT controller
        write(device_dir.join("device"), "0x9a27\n").unwrap();
        let results = find_tbt_controllers(root);
        assert_eq!(results, Some(vec![device_dir.clone()]));

        // Create a 0x9a28 TBT controller
        write(device_dir.join("device"), "0x9a29\n").unwrap();
        let results = find_tbt_controllers(root);
        assert_eq!(results, Some(vec![device_dir.clone()]));

        // Create a mismatching TBT controller
        write(device_dir.join("device"), "0x1234\n").unwrap();

        let results = find_tbt_controllers(root);
        assert_eq!(results, None);

        // Create multiple mock PCI device directories
        write(device_dir.join("device"), "0x1234\n").unwrap();

        let device_dir1 = root.join("sys/devices/pci0000:00/0000:00:01.0");
        create_dir_all(&device_dir1).unwrap();
        write(device_dir1.join("device"), "0x9a25\n").unwrap();

        let results = find_tbt_controllers(root);
        assert_eq!(results, Some(vec![device_dir1]));
    }
}
