// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Display;
use std::fs::File;
use std::io;
use std::io::Write;
use std::path::Path;
use std::path::PathBuf;

use crate::ProcessId;
use crate::ThreadId;

const CGROUP_CPU_PATH: &str = "/sys/fs/cgroup/cpu";
const CGROUP_CPUSET_PATH: &str = "/sys/fs/cgroup/cpuset";
const CPU_SHARE_FILE: &str = "cpu.shares";
const CPUS_FILE: &str = "cpus";
const CGROUP_PROCESSES_FILE: &str = "cgroup.procs";
const CGROUP_THREADS_FILE: &str = "tasks";

/// Error while setting up cgroups
#[derive(Debug)]
pub struct CgroupSetupError(PathBuf, io::Error);

/// Cgroup setup result returns [File].
pub type CgroupSetupResult = std::result::Result<File, CgroupSetupError>;

impl std::error::Error for CgroupSetupError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        Some(&self.1)
    }
}

impl Display for CgroupSetupError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!(
            "failed setup cgroup: {:?}: {}",
            self.0, self.1
        ))
    }
}

/// Setup cpu cgroup
///
/// Cpu cgroups are used to control cpu share of managed processes.
///
/// This creates the cgroup if not exist.
///
/// This returns an opened [File] of "cgroup.procs" of the cgroup.
///
/// TODO(kawasin): Write unit test. The test requires an environment to execute
/// with cpu cgroup submodule enabled.
pub fn setup_cpu_cgroup(name: &str, cpu_shares: u16) -> CgroupSetupResult {
    let cgroup_path = Path::new(CGROUP_CPU_PATH).join(name);
    if !cgroup_path.exists() {
        if let Err(e) = std::fs::create_dir_all(&cgroup_path) {
            return Err(CgroupSetupError(cgroup_path, e));
        }
    }
    let share_file_path = cgroup_path.join(CPU_SHARE_FILE);
    std::fs::write(&share_file_path, cpu_shares.to_string())
        .map_err(|e| CgroupSetupError(share_file_path, e))?;
    let cgroup_file_path = cgroup_path.join(CGROUP_PROCESSES_FILE);
    std::fs::OpenOptions::new()
        .write(true)
        .open(&cgroup_file_path)
        .map_err(|e| CgroupSetupError(cgroup_file_path, e))
}

/// Setup cpuset cgroup
///
/// Cpuset cgroups are used to control cpu cores for managed threads.
///
/// This creates the cgroup if not exist.
///
/// This returns an opened [File] of "tasks" of the cgroup.
///
/// TODO(kawasin): Write unit test. The test requires an environment to execute
/// with cpuset cgroup submodule enabled.
pub fn setup_cpuset_cgroup(name: &str, cpus: &str) -> CgroupSetupResult {
    let cgroup_path = Path::new(CGROUP_CPUSET_PATH).join(name);
    if !cgroup_path.exists() {
        if let Err(e) = std::fs::create_dir_all(&cgroup_path) {
            return Err(CgroupSetupError(cgroup_path, e));
        }
    }
    let cpus_file_path = cgroup_path.join(CPUS_FILE);
    std::fs::write(&cpus_file_path, cpus).map_err(|e| CgroupSetupError(cpus_file_path, e))?;
    let cgroup_file_path = cgroup_path.join(CGROUP_THREADS_FILE);
    std::fs::OpenOptions::new()
        .write(true)
        .open(&cgroup_file_path)
        .map_err(|e| CgroupSetupError(cgroup_file_path, e))
}

/// Set of cgroups used for scheduler settings.
///
/// cpu cgroups are used for [CpuCgroup]. The files must points "cgroup.procs"
/// file of each cpu cgroup.
///
/// cpuset cgroups are used for [CpusetCgroup]. The files must points "tasks"
/// file of each cpuset cgroup.
#[derive(Debug)]
pub struct CgroupContext {
    /// cgroup.procs file of cpu cgroup for normal processes
    pub cpu_normal: File,
    /// cgroup.procs file of cpu cgroup for background processes
    pub cpu_background: File,
    /// tasks file of cpuset cgroup for threads using all CPU cores
    pub cpuset_all: File,
    /// tasks file of cpuset cgroup for threads using efficient CPU cores only
    pub cpuset_efficient: File,
}

impl CgroupContext {
    pub(crate) fn set_cpu_cgroup(
        &mut self,
        process_id: ProcessId,
        cpu_cgroup: CpuCgroup,
    ) -> io::Result<()> {
        let cgroup_file = match cpu_cgroup {
            CpuCgroup::Normal => &mut self.cpu_normal,
            CpuCgroup::Background => &mut self.cpu_background,
        };

        let _ = cgroup_file.write(process_id.0.to_string().as_bytes())?;
        Ok(())
    }

    pub(crate) fn set_cpuset_cgroup(
        &mut self,
        thread_id: ThreadId,
        cpuset_cgroup: CpusetCgroup,
        allow_foreground_cpuset: bool,
    ) -> io::Result<()> {
        let cgroup_file = match cpuset_cgroup {
            CpusetCgroup::All => &mut self.cpuset_all,
            CpusetCgroup::Foreground => {
                if allow_foreground_cpuset {
                    &mut self.cpuset_all
                } else {
                    &mut self.cpuset_efficient
                }
            }
            CpusetCgroup::Efficient => &mut self.cpuset_efficient,
        };

        let _ = cgroup_file.write(thread_id.0.to_string().as_bytes())?;
        Ok(())
    }
}

/// Cpu cgroups
#[derive(Clone, Copy, Debug)]
pub enum CpuCgroup {
    Normal,
    Background,
}

impl CpuCgroup {
    /// Name to display
    pub fn name(&self) -> &'static str {
        match self {
            Self::Normal => "cpu.normal",
            Self::Background => "cpu.background",
        }
    }
}

/// Cpuset cgroups
#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub enum CpusetCgroup {
    /// Runs on all cores
    All,
    /// Runs on all cores when foreground. Use efficient cores only when
    /// background.
    Foreground,
    /// Runs on efficient cores only
    Efficient,
}

impl CpusetCgroup {
    /// Name to display
    pub fn name(&self) -> &'static str {
        match self {
            Self::All => "cpuset.all",
            Self::Foreground => "cpuset.foreground",
            Self::Efficient => "cpuset.efficient",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::*;

    #[test]
    fn test_set_cpu_cgroup() {
        let (mut ctx, mut files) = create_fake_cgroup_context_pair();

        ctx.set_cpu_cgroup(ProcessId(123), CpuCgroup::Normal)
            .unwrap();
        assert_eq!(read_number(&mut files.cpu_normal), Some(123));

        ctx.set_cpu_cgroup(ProcessId(456), CpuCgroup::Normal)
            .unwrap();
        assert_eq!(read_number(&mut files.cpu_normal), Some(456));

        ctx.set_cpu_cgroup(ProcessId(789), CpuCgroup::Background)
            .unwrap();
        assert_eq!(read_number(&mut files.cpu_normal), None);
        assert_eq!(read_number(&mut files.cpu_background), Some(789));
    }

    #[test]
    fn test_set_cpuset_cgroup() {
        let (mut ctx, mut files) = create_fake_cgroup_context_pair();

        ctx.set_cpuset_cgroup(ThreadId(1), CpusetCgroup::All, true)
            .unwrap();
        assert_eq!(read_number(&mut files.cpuset_all), Some(1));
        ctx.set_cpuset_cgroup(ThreadId(2), CpusetCgroup::All, false)
            .unwrap();
        assert_eq!(read_number(&mut files.cpuset_all), Some(2));

        ctx.set_cpuset_cgroup(ThreadId(3), CpusetCgroup::Foreground, true)
            .unwrap();
        assert_eq!(read_number(&mut files.cpuset_all), Some(3));
        ctx.set_cpuset_cgroup(ThreadId(4), CpusetCgroup::Foreground, false)
            .unwrap();
        assert_eq!(read_number(&mut files.cpuset_efficient), Some(4));

        ctx.set_cpuset_cgroup(ThreadId(5), CpusetCgroup::Efficient, true)
            .unwrap();
        assert_eq!(read_number(&mut files.cpuset_efficient), Some(5));

        ctx.set_cpuset_cgroup(ThreadId(6), CpusetCgroup::Efficient, true)
            .unwrap();
        assert_eq!(read_number(&mut files.cpuset_efficient), Some(6));
    }
}
