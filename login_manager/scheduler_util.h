// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_SCHEDULER_UTIL_H_
#define LOGIN_MANAGER_SCHEDULER_UTIL_H_

#include <string>
#include <vector>

#include <chromeos-config/libcros_config/cros_config_interface.h>

namespace base {
class FilePath;
}

namespace login_manager {

// Implementation func to calculate hybrid and get small core cpu id list based
// on the attribute (either cpu_capacity or cpuinfo_max_freq). If there are more
// than two capacities or two freqs, we consider the cpus with two smallest
// capacities / freqs as small cores.
// Returns non-empty cpu id list on success. Returns an empty list on any error
// or non-hybrid cpu arch.
std::vector<std::string> GetSmallCoreCpuIdsFromAttr(
    const base::FilePath& cpu_bus_dir, base::StringPiece attribute);

// Calculates the number of cpu_capacity or cpu_freq and gets small core cpu id
// list if the cpu arch is hybrid. It calls the impl func
// GetSmallCoreCpuIdsFromAttr to do calculations.
// Returns non-empty cpu id list on success. Returns an empty list on any error
// or non-hybrid cpu arch.
std::vector<std::string> CalculateSmallCoreCpusIfHybrid(
    const base::FilePath& cpu_bus_dir);

// If the cpu arch is hybrid, writes the mask of small cores to non-urgent
// cpuset and restricts non-urgent threads to small cores.
// Returns true on success.
bool ConfigureNonUrgentCpuset(brillo::CrosConfigInterface* cros_config);

}  // namespace login_manager

#endif  // LOGIN_MANAGER_SCHEDULER_UTIL_H_
