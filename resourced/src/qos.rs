// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use schedqos::cgroups::open_cpuset_cgroup;
use schedqos::cgroups::setup_cpu_cgroup;
use schedqos::CgroupContext;
use schedqos::Config;
use schedqos::SchedQosContext;

pub fn create_schedqos_context() -> anyhow::Result<SchedQosContext> {
    let cpu_normal = setup_cpu_cgroup("resourced/normal", 1024)?;
    let cpu_background = setup_cpu_cgroup("resourced/background", 10)?;
    // Note these might be changed to resourced specific folders in the futre
    let cpuset_all = open_cpuset_cgroup("chrome/urgent")?;
    let cpuset_efficient = open_cpuset_cgroup("chrome/non-urgent")?;

    let ctx = SchedQosContext::new(Config {
        cgroup_context: CgroupContext {
            cpu_normal,
            cpu_background,
            cpuset_all,
            cpuset_efficient,
        },
        process_configs: Config::default_process_config(),
        thread_configs: Config::default_thread_config(),
    })?;
    Ok(ctx)
}
