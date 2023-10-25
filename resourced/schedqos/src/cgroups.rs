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
/// This returns an opened [File] of cgroup.procs of the cgroup.
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
    let share_file = cgroup_path.join(CPU_SHARE_FILE);
    std::fs::write(&share_file, cpu_shares.to_string())
        .map_err(|e| CgroupSetupError(share_file, e))?;
    let cgroup_file = cgroup_path.join(CGROUP_PROCESSES_FILE);
    std::fs::OpenOptions::new()
        .write(true)
        .open(&cgroup_file)
        .map_err(|e| CgroupSetupError(cgroup_file, e))
}

/// Opens tasks file of the existing cpuset cgroup
///
/// The cpuset cgroup must be configured.
///
/// TODO(kawasin): Move cpuset setup into resourced
/// TODO(kawasin): Write unit test. The test requires an environment to execute
/// with cpu cgroup submodule enabled.
pub fn open_cpuset_cgroup(name: &str) -> CgroupSetupResult {
    let cgroup_path = Path::new(CGROUP_CPUSET_PATH)
        .join(name)
        .join(CGROUP_THREADS_FILE);
    std::fs::OpenOptions::new()
        .write(true)
        .open(&cgroup_path)
        .map_err(|e| CgroupSetupError(cgroup_path, e))
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
    ) -> io::Result<()> {
        let cgroup_file = match cpuset_cgroup {
            CpusetCgroup::All => &mut self.cpuset_all,
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
#[derive(Clone, Copy, Debug)]
pub enum CpusetCgroup {
    All,
    Efficient,
}

impl CpusetCgroup {
    /// Name to display
    pub fn name(&self) -> &'static str {
        match self {
            Self::All => "cpuset.all",
            Self::Efficient => "cpuset.efficient",
        }
    }
}

#[cfg(test)]
mod tests {

    use std::io::Read;

    use super::*;

    #[derive(Debug)]
    struct FakeCgroupFiles {
        cpu_normal: File,
        cpu_background: File,
        cpuset_all: File,
        cpuset_efficient: File,
    }

    fn create_fake_cgroup_context_pair() -> (CgroupContext, FakeCgroupFiles) {
        let cpu_normal = tempfile::NamedTempFile::new().unwrap();
        let cpu_background = tempfile::NamedTempFile::new().unwrap();
        let cpuset_all = tempfile::NamedTempFile::new().unwrap();
        let cpuset_efficient = tempfile::NamedTempFile::new().unwrap();
        (
            CgroupContext {
                cpu_normal: cpu_normal.reopen().unwrap(),
                cpu_background: cpu_background.reopen().unwrap(),
                cpuset_all: cpuset_all.reopen().unwrap(),
                cpuset_efficient: cpuset_efficient.reopen().unwrap(),
            },
            FakeCgroupFiles {
                cpu_normal: cpu_normal.reopen().unwrap(),
                cpu_background: cpu_background.reopen().unwrap(),
                cpuset_all: cpuset_all.reopen().unwrap(),
                cpuset_efficient: cpuset_efficient.reopen().unwrap(),
            },
        )
    }

    #[test]
    fn test_set_cpu_cgroup() {
        let (mut ctx, mut files) = create_fake_cgroup_context_pair();
        let mut buf = [0; 1024];

        ctx.set_cpu_cgroup(ProcessId(123), CpuCgroup::Normal)
            .unwrap();
        assert_eq!(files.cpu_normal.read(buf.as_mut_slice()).unwrap(), 3);
        assert_eq!(&buf[..3], b"123");

        ctx.set_cpu_cgroup(ProcessId(456), CpuCgroup::Normal)
            .unwrap();
        assert_eq!(files.cpu_normal.read(buf.as_mut_slice()).unwrap(), 3);
        assert_eq!(&buf[..3], b"456");

        ctx.set_cpu_cgroup(ProcessId(789), CpuCgroup::Background)
            .unwrap();
        assert_eq!(files.cpu_background.read(buf.as_mut_slice()).unwrap(), 3);
        assert_eq!(&buf[..3], b"789");
    }

    #[test]
    fn test_set_cpuset_cgroup() {
        let (mut ctx, mut files) = create_fake_cgroup_context_pair();
        let mut buf = [0; 1024];

        ctx.set_cpuset_cgroup(ThreadId(123), CpusetCgroup::All)
            .unwrap();
        assert_eq!(files.cpuset_all.read(buf.as_mut_slice()).unwrap(), 3);
        assert_eq!(&buf[..3], b"123");

        ctx.set_cpuset_cgroup(ThreadId(456), CpusetCgroup::All)
            .unwrap();
        assert_eq!(files.cpuset_all.read(buf.as_mut_slice()).unwrap(), 3);
        assert_eq!(&buf[..3], b"456");

        ctx.set_cpuset_cgroup(ThreadId(789), CpusetCgroup::Efficient)
            .unwrap();
        assert_eq!(files.cpuset_efficient.read(buf.as_mut_slice()).unwrap(), 3);
        assert_eq!(&buf[..3], b"789");
    }
}
