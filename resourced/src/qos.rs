// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

use schedqos::CgroupConfig;
use schedqos::Config;
use schedqos::SchedQosContext;

// TODO(kawasin): Pass the path to cgroup directory (not including cgroup.procs)
const CGROUP_CPU_NORMAL: &str = "/sys/fs/cgroup/cpu/resourced/normal/cgroup.procs";
const CGROUP_CPU_BACKGROUND: &str = "/sys/fs/cgroup/cpu/resourced/background/cgroup.procs";

// TODO(kawasin): Pass the path to cgroup directory (not including tasks)
// Note these might be changed to resourced specific folders in the futre
const CGROUP_CPUSET_ALL: &str = "/sys/fs/cgroup/cpuset/chrome/urgent/tasks";
const CGROUP_CPUSET_EFFICIENT: &str = "/sys/fs/cgroup/cpuset/chrome/non-urgent/tasks";

pub fn create_schedqos_context() -> anyhow::Result<SchedQosContext> {
    SchedQosContext::new(Config {
        cgroup_config: CgroupConfig {
            cpu_normal: PathBuf::from(CGROUP_CPU_NORMAL),
            cpu_background: PathBuf::from(CGROUP_CPU_BACKGROUND),
            cpuset_all: PathBuf::from(CGROUP_CPUSET_ALL),
            cpuset_efficient: PathBuf::from(CGROUP_CPUSET_EFFICIENT),
        },
        process_configs: Config::default_process_config(),
        thread_configs: Config::default_thread_config(),
    })
}
