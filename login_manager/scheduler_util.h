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

// Checks if the big_little flag exists in file.
bool HasHybridFlag(const base::FilePath& flags_file);

// Implementation func to get small core cpu id list based on the attribute
// (either cpu_capacity or cpuinfo_max_freq). Small cores have the smallest
// capacity or freq in hybrid arch.
// Returns non-empty list on success. Returns an empty list on any error or
// non-hybrid cpu arch.
std::vector<std::string> GetSmallCoreCpuIdsFromAttr(
    const base::FilePath& cpu_bus_dir, base::StringPiece attribute);

// Gets small core cpu id list based on cpu_capacity or cpu_freq via sysfs. It
// calls the impl func GetSmallCoreCpuIdsFromAttr to do calculations.
// Returns non-empty list on success. Returns an empty list on any error or
// non-hybrid cpu arch.
std::vector<std::string> CalculateSmallCoreCpus(
    const base::FilePath& cpu_bus_dir);

// Writes the mask of small cores to non-urgent cpuset and restrict non-urgent
// threads to small cores. Returns true on success.
bool ConfigureNonUrgentCpuset(brillo::CrosConfigInterface* cros_config);

}  // namespace login_manager

#endif  // LOGIN_MANAGER_SCHEDULER_UTIL_H_
