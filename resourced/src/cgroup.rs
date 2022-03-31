// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use std::path::{Path, PathBuf};

// Below table can keep track of Cgroup/Cpuset sysfs resourced is interested in.
// Please add below if there is any need.
pub const CGROUP_CPUSET_SYSFS_NUM: usize = 4;
pub const CGROUP_CPUSET_SYSFS: [&str; CGROUP_CPUSET_SYSFS_NUM] = [
    "sys/fs/cgroup/cpuset/chrome/urgent/cpus",
    "sys/fs/cgroup/cpuset/chrome/non-urgent/cpus",
    "sys/fs/cgroup/cpuset/chrome/cpus",
    "sys/fs/cgroup/cpuset/media/cpus",
];

#[derive(Debug)]
pub struct CgroupCpuset {
    // Cgroup/Cpuset sysfs path.
    sysfs_path: PathBuf,
    // Default "cpus" of Cgroup/Cpuset.
    default_cpus: String,
}

impl CgroupCpuset {
    pub fn new(root: &Path, path: &str) -> Result<Self> {
        let sysfs_path = root.join(path);

        //Read the platform's default "cpus" and save it locally.
        let default_cpus = Self::read(&sysfs_path)?;

        Ok(CgroupCpuset {
            sysfs_path,
            default_cpus,
        })
    }

    // Read "cpus" from sysfs.
    fn read(path: &PathBuf) -> Result<String> {
        Ok(std::fs::read_to_string(path)?)
    }

    // Write a new "cpus" to sysfs.
    fn write(&self, cpus: &str) -> Result<()> {
        std::fs::write(&self.sysfs_path, cpus)?;
        Ok(())
    }

    // Write back platform's default "cpus" to sysfs.
    fn restore(&self) -> Result<()> {
        std::fs::write(&self.sysfs_path, &self.default_cpus)?;
        Ok(())
    }
}

#[derive(Debug)]
pub struct CgroupCpusetManager {
    cpuset_list: Vec<CgroupCpuset>,
}

impl CgroupCpusetManager {
    pub fn new(root: PathBuf) -> Result<Self> {
        let mut cpuset_list = Vec::new();

        // Create CgroupCpuset per sysfs.
        for sysfs_path in CGROUP_CPUSET_SYSFS.iter() {
            cpuset_list.push(CgroupCpuset::new(&root, sysfs_path)?);
        }

        Ok(CgroupCpusetManager { cpuset_list })
    }

    // Write a new "cpus" to all entries.
    pub fn write_all(&self, cpus: &str) -> Result<()> {
        for item in self.cpuset_list.iter() {
            item.write(cpus)?
        }
        Ok(())
    }

    // Write back platfrom's default "cpus" to all entries.
    pub fn restore_all(&self) -> Result<()> {
        for item in self.cpuset_list.iter() {
            item.restore()?
        }
        Ok(())
    }
}
