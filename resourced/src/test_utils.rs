// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::path::Path;
use std::path::PathBuf;
use std::time::Duration;

use anyhow::Result;

use crate::common::BatterySaverMode;
use crate::common::FullscreenVideo;
use crate::common::GameMode;
use crate::common::RTCAudioActive;
use crate::common::ThermalState;
use crate::common::VmBootMode;
pub use crate::config::FakeConfig;
use crate::config::Governor;
use crate::config::PowerSourceType;
use crate::cpu_utils::SMT_CONTROL_PATH;
use crate::power;
use crate::power::PowerSourceProvider;

const MOCK_NUM_CPU: i32 = 16;

pub const CPUINFO_PATH: &str = "proc/cpuinfo";
/// Base path for power_limit relative to rootdir.
pub const DEVICE_POWER_LIMIT_PATH: &str = "sys/class/powercap/intel-rapl:0";

/// Base path for cpufreq relative to rootdir.
pub const DEVICE_CPUFREQ_PATH: &str = "sys/devices/system/cpu/cpufreq";

// Device path for GPU card.
pub const GPU0_DEVICE_PATH: &str = "sys/class/drm/card0";

// Device path for GPU RPS path
pub const GPU0_RPS_DEVICE_PATH: &str = "sys/class/drm/card0/gt/gt0";
pub const GPU0_RPS_DEFAULT_DEVICE_PATH: &str = "sys/class/drm/card0/gt/gt0/.defaults";

pub struct MockPowerPreferencesManager {
    pub root: PathBuf,
}
impl power::PowerPreferencesManager for MockPowerPreferencesManager {
    fn update_power_preferences(
        &self,
        _rtc: RTCAudioActive,
        _fullscreen: FullscreenVideo,
        _game: GameMode,
        _vmboot: VmBootMode,
        _batterysaver: BatterySaverMode,
        _thermalstate: ThermalState,
    ) -> Result<()> {
        Ok(())
    }
    fn get_root(&self) -> &Path {
        self.root.as_path()
    }
}

pub fn test_create_parent_dir(path: &Path) {
    fs::create_dir_all(path.parent().unwrap()).unwrap();
}

pub fn test_write_online_cpu(root: &Path, cpu: u32, value: &str) {
    let root_online_cpu = root.join(format!("sys/devices/system/cpu/cpu{}/online", cpu));
    test_create_parent_dir(&root_online_cpu);
    fs::write(root_online_cpu, value).unwrap();
}

pub fn test_check_online_cpu(root: &Path, cpu: u32, expected: &str) {
    let root_online_cpu = root.join(format!("sys/devices/system/cpu/cpu{}/online", cpu));
    test_create_parent_dir(&root_online_cpu);
    let value = fs::read_to_string(root_online_cpu).unwrap();
    assert_eq!(value, expected);
}

pub fn test_write_core_cpus_list(root: &Path, cpu: u32, value: &str) {
    let core_cpus_list = root.join(format!(
        "sys/devices/system/cpu/cpu{}/topology/core_cpus_list",
        cpu
    ));
    test_create_parent_dir(&core_cpus_list);
    fs::write(core_cpus_list, value).unwrap();
}

pub fn test_write_smt_control(root: &Path, status: &str) {
    let smt_control = root.join(SMT_CONTROL_PATH);
    test_create_parent_dir(&smt_control);
    fs::write(smt_control, status).unwrap();
}

pub fn test_check_smt_control(root: &Path, expected: &str) {
    let root_smt_control = root.join(SMT_CONTROL_PATH);
    test_create_parent_dir(&root_smt_control);
    let value = fs::read_to_string(root_smt_control).unwrap();
    assert_eq!(value, expected);
}

pub fn test_write_cpuset_root_cpus(root: &Path, cpus: &str) {
    let root_cpuset_cpus = root.join("sys/fs/cgroup/cpuset/cpus");
    test_create_parent_dir(&root_cpuset_cpus);
    fs::write(root_cpuset_cpus, cpus).unwrap();
}

pub fn test_write_cpu_max_freq(root: &Path, cpu_num: u32, max_freq: u32) {
    let cpu_max_path = root.join(format!(
        "sys/bus/cpu/devices/cpu{}/cpufreq/cpuinfo_max_freq",
        cpu_num
    ));
    test_create_parent_dir(&cpu_max_path);
    fs::write(cpu_max_path, max_freq.to_string()).unwrap();
}

pub fn write_mock_pl0(root: &Path, value: u64) -> Result<()> {
    fs::write(
        root.join(DEVICE_POWER_LIMIT_PATH)
            .join("constraint_0_power_limit_uw"),
        value.to_string(),
    )?;

    Ok(())
}

pub fn write_mock_cpu(
    root: &Path,
    cpu_num: i32,
    baseline_max: u64,
    curr_max: u64,
    baseline_min: u64,
    curr_min: u64,
) -> Result<()> {
    let policy_path = root
        .join(DEVICE_CPUFREQ_PATH)
        .join(format!("policy{cpu_num}"));
    fs::write(
        policy_path.join("cpuinfo_max_freq"),
        baseline_max.to_string(),
    )
    .expect("Failed to write to file!");
    fs::write(
        policy_path.join("cpuinfo_min_freq"),
        baseline_min.to_string(),
    )
    .expect("Failed to write to file!");

    fs::write(policy_path.join("scaling_max_freq"), curr_max.to_string())?;
    fs::write(policy_path.join("scaling_min_freq"), curr_min.to_string())?;
    Ok(())
}

pub fn setup_mock_cpu_dev_dirs(root: &Path) -> anyhow::Result<()> {
    fs::create_dir_all(root.join(DEVICE_POWER_LIMIT_PATH))?;
    for i in 0..MOCK_NUM_CPU {
        fs::create_dir_all(root.join(DEVICE_CPUFREQ_PATH).join(format!("policy{i}")))?;
    }
    Ok(())
}

pub fn get_cpu0_freq_max(root: &Path) -> i32 {
    let policy_path = root.join(DEVICE_CPUFREQ_PATH).join("policy0");
    let read_val = fs::read(policy_path.join("scaling_max_freq")).unwrap();

    std::str::from_utf8(&read_val)
        .unwrap()
        .parse::<i32>()
        .unwrap()
}

pub fn get_cpu0_freq_min(root: &Path) -> i32 {
    let policy_path = root.join(DEVICE_CPUFREQ_PATH).join("policy0");
    let read_val = fs::read(policy_path.join("scaling_min_freq")).unwrap();

    std::str::from_utf8(&read_val)
        .unwrap()
        .parse::<i32>()
        .unwrap()
}

pub fn setup_mock_cpu_files(root: &Path) -> Result<()> {
    let pl_files: Vec<&str> = vec![
        "constraint_0_power_limit_uw",
        "constraint_0_max_power_uw",
        "constraint_1_power_limit_uw",
        "constraint_1_max_power_uw",
        "energy_uj",
        "max_energy_range_uj",
    ];

    let cpufreq_files: Vec<(&str, &str)> = vec![
        ("scaling_max_freq", "4100000"),
        ("cpuinfo_max_freq", "4100000"),
        ("scaling_min_freq", "400000"),
        ("cpuinfo_min_freq", "400000"),
        ("base_frequency", "2100000"),
    ];

    for pl_file in &pl_files {
        fs::write(
            root.join(DEVICE_POWER_LIMIT_PATH)
                .join(PathBuf::from(pl_file)),
            "0",
        )?;
    }

    for i in 0..MOCK_NUM_CPU {
        let policy_path = root.join(DEVICE_CPUFREQ_PATH).join(format!("policy{i}"));

        for cpufreq_file in &cpufreq_files {
            fs::write(policy_path.join(cpufreq_file.0), cpufreq_file.1)?;
        }
    }

    Ok(())
}

pub fn construct_poc_cpuinfo_snippet(vendor: &str, model_name: &str) -> String {
    format!(
        r#"
processor       : 0
vendor_id       : {vendor}
cpu family      : 23
model           : 24
model name      : {model_name}
stepping        : 1
microcode       : 0x8108109

processor       : 1
vendor_id       : {vendor}
cpu family      : 25
model           : 24
model name      : {model_name}"#
    )
}

pub fn write_mock_cpuinfo(root: &Path, vendor: &str, model_name: &str) {
    fs::write(
        root.join(CPUINFO_PATH),
        construct_poc_cpuinfo_snippet(vendor, model_name),
    )
    .unwrap();
}

pub fn setup_mock_intel_gpu_dev_dirs(root: &Path) {
    fs::create_dir_all(root.join(CPUINFO_PATH).parent().unwrap()).unwrap();
    fs::create_dir_all(root.join(GPU0_RPS_DEVICE_PATH)).unwrap();
    fs::create_dir_all(root.join(GPU0_RPS_DEFAULT_DEVICE_PATH)).unwrap();
}

pub fn setup_mock_intel_gpu_files(root: &Path) {
    let gpu_files = vec![
        ("gt_min_freq_mhz", 200),
        ("gt_max_freq_mhz", 1000),
        ("gt_boost_freq_mhz", 1000),
    ];

    for (gpu_file, default_freq) in &gpu_files {
        fs::write(
            root.join(GPU0_DEVICE_PATH).join(PathBuf::from(gpu_file)),
            default_freq.to_string(),
        )
        .unwrap();
    }

    let rps_files = vec![("rps_up_threshold_pct", 85), ("rps_down_threshold_pct", 95)];

    for (rps_file, default_val) in &rps_files {
        for base_path in [GPU0_RPS_DEVICE_PATH, GPU0_RPS_DEFAULT_DEVICE_PATH] {
            fs::write(
                root.join(base_path).join(PathBuf::from(rps_file)),
                default_val.to_string(),
            )
            .unwrap();
        }
    }
}

pub fn get_intel_gpu_max(root: &Path) -> i32 {
    let gpu_max_path = root.join(GPU0_DEVICE_PATH).join("gt_max_freq_mhz");
    let read_val = fs::read(gpu_max_path).unwrap();
    std::str::from_utf8(&read_val)
        .unwrap()
        .parse::<i32>()
        .unwrap()
}

pub fn set_intel_gpu_max(root: &Path, val: u32) {
    let gpu_max_path = root.join(GPU0_DEVICE_PATH).join("gt_max_freq_mhz");
    fs::write(gpu_max_path, val.to_string()).unwrap();
}

pub fn set_intel_gpu_min(root: &Path, val: u32) {
    let gpu_min_path = root.join(GPU0_DEVICE_PATH).join("gt_min_freq_mhz");
    fs::write(gpu_min_path, val.to_string()).unwrap();
}

pub fn get_intel_gpu_boost(root: &Path) -> i32 {
    let gpu_max_path = root.join(GPU0_DEVICE_PATH).join("gt_boost_freq_mhz");
    let read_val = fs::read(gpu_max_path).unwrap();
    std::str::from_utf8(&read_val)
        .unwrap()
        .parse::<i32>()
        .unwrap()
}

pub fn set_intel_gpu_boost(root: &Path, val: u32) {
    let gpu_boot_path = root.join(GPU0_DEVICE_PATH).join("gt_boost_freq_mhz");
    fs::write(gpu_boot_path, val.to_string()).unwrap();
}

pub struct ProcessForTest {
    process_id: u32,
}

impl Drop for ProcessForTest {
    fn drop(&mut self) {
        let process_id = self.process_id as libc::pid_t;
        unsafe {
            libc::kill(process_id, libc::SIGKILL);
            libc::waitpid(process_id, std::ptr::null_mut(), 0);
        }
    }
}

pub fn fork_process_for_test() -> (u32, ProcessForTest) {
    let child_process_id = unsafe { libc::fork() };
    if child_process_id == 0 {
        loop {
            std::thread::sleep(Duration::from_secs(1));
        }
    }
    assert!(child_process_id > 0);
    let child_process_id = child_process_id as u32;
    (
        child_process_id,
        ProcessForTest {
            process_id: child_process_id,
        },
    )
}

pub struct FakePowerSourceProvider {
    pub power_source: PowerSourceType,
}

impl PowerSourceProvider for FakePowerSourceProvider {
    fn get_power_source(&self) -> Result<PowerSourceType> {
        Ok(self.power_source)
    }
}

// In the following per policy access functions, there are 2 cpufreq policies: policy0 and
// policy1.

pub const TEST_CPUFREQ_POLICIES: &[&str] = &[
    "sys/devices/system/cpu/cpufreq/policy0",
    "sys/devices/system/cpu/cpufreq/policy1",
];
pub const SCALING_GOVERNOR_FILENAME: &str = "scaling_governor";
pub const ONDEMAND_DIRECTORY: &str = "ondemand";
pub const POWERSAVE_BIAS_FILENAME: &str = "powersave_bias";
pub const SAMPLING_RATE_FILENAME: &str = "sampling_rate";
pub const AFFECTED_CPUS_NAME: &str = "affected_cpus";
pub const AFFECTED_CPU_NONE: &str = "";
pub const AFFECTED_CPU0: &str = "0";
pub const AFFECTED_CPU1: &str = "1";

pub struct PolicyConfigs<'a> {
    pub policy_path: &'a str,
    pub governor: &'a Governor,
    pub affected_cpus: &'a str,
}
// Instead of returning an error, crash/assert immediately in a test utility function makes it
// easier to debug an unittest.
pub fn write_per_policy_scaling_governor(root: &Path, policies: Vec<PolicyConfigs>) {
    for policy in policies {
        let policy_path = root.join(policy.policy_path);
        fs::create_dir_all(&policy_path).unwrap();
        std::fs::write(
            policy_path.join(SCALING_GOVERNOR_FILENAME),
            policy.governor.name().to_string() + "\n",
        )
        .unwrap();
        std::fs::write(
            policy_path.join(AFFECTED_CPUS_NAME),
            policy.affected_cpus.to_owned() + "\n",
        )
        .unwrap();
    }
}

pub fn check_per_policy_scaling_governor(root: &Path, expected: Vec<Governor>) {
    for (i, policy) in TEST_CPUFREQ_POLICIES.iter().enumerate() {
        let governor_path = root.join(policy).join(SCALING_GOVERNOR_FILENAME);
        let scaling_governor = std::fs::read_to_string(governor_path).unwrap();
        assert_eq!(scaling_governor.trim_end_matches('\n'), expected[i].name());
    }
}

pub fn write_per_policy_powersave_bias(root: &Path, value: u32) {
    for policy in TEST_CPUFREQ_POLICIES {
        let ondemand_path = root.join(policy).join(ONDEMAND_DIRECTORY);
        println!("ondemand_path: {}", ondemand_path.display());
        fs::create_dir_all(&ondemand_path).unwrap();
        std::fs::write(
            ondemand_path.join(POWERSAVE_BIAS_FILENAME),
            value.to_string() + "\n",
        )
        .unwrap();
    }
}

pub fn check_per_policy_powersave_bias(root: &Path, expected: u32) {
    for policy in TEST_CPUFREQ_POLICIES {
        let powersave_bias_path = root
            .join(policy)
            .join(ONDEMAND_DIRECTORY)
            .join(POWERSAVE_BIAS_FILENAME);
        let powersave_bias = std::fs::read_to_string(powersave_bias_path).unwrap();
        assert_eq!(powersave_bias.trim_end_matches('\n'), expected.to_string());
    }
}

pub fn write_per_policy_sampling_rate(root: &Path, value: u32) {
    for policy in TEST_CPUFREQ_POLICIES {
        let ondemand_path = root.join(policy).join(ONDEMAND_DIRECTORY);
        fs::create_dir_all(&ondemand_path).unwrap();
        std::fs::write(
            ondemand_path.join(SAMPLING_RATE_FILENAME),
            value.to_string(),
        )
        .unwrap();
    }
}

pub fn check_per_policy_sampling_rate(root: &Path, expected: u32) {
    for policy in TEST_CPUFREQ_POLICIES {
        let sampling_rate_path = root
            .join(policy)
            .join(ONDEMAND_DIRECTORY)
            .join(SAMPLING_RATE_FILENAME);
        let sampling_rate = std::fs::read_to_string(sampling_rate_path).unwrap();
        assert_eq!(sampling_rate, expected.to_string());
    }
}
