// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/scheduler_util.h"

#include <algorithm>
#include <numeric>
#include <set>
#include <stdlib.h>
#include <utility>

#include <base/containers/contains.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace {

// Size of the prefix 'cpu'.
constexpr size_t kCpuPrefixSize = 3;

constexpr char kCpuBusDir[] = "/sys/bus/cpu/devices";
constexpr char kCpuCapFile[] = "cpu_capacity";
constexpr char kCpuMaxFreqFile[] = "cpufreq/cpuinfo_max_freq";
constexpr char kCpusetNonUrgentDir[] =
    "/sys/fs/cgroup/cpuset/chrome/non-urgent";
constexpr char kUseFlagsFile[] = "/etc/ui_use_flags.txt";

}  // namespace

namespace login_manager {

bool HasHybridFlag(const base::FilePath& flags_file) {
  std::string content;
  if (!base::ReadFileToString(flags_file, &content)) {
    LOG(ERROR) << "Error reading the file: " << flags_file << " !";
    return false;
  }

  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  return base::Contains(lines, "big_little");
}

std::vector<std::string> GetSmallCoreCpuIdsFromAttr(
    const base::FilePath& cpu_bus_dir, base::StringPiece attribute) {
  base::FilePath cpu0_attr_file = cpu_bus_dir.Append("cpu0").Append(attribute);
  if (!base::PathExists(cpu0_attr_file)) {
    return {};
  }

  std::string min_item_str;
  if (!base::ReadFileToString(cpu0_attr_file, &min_item_str)) {
    LOG(ERROR) << "Error reading cpu0 attribute file!";
    return {};
  }
  int min_item = atoi(min_item_str.c_str());
  if (min_item <= 0) {
    LOG(ERROR) << "Invalid value read from cpu0 attribute file!";
    return {};
  }

  // Gets small cpu ids through traversing the attribute (cpu_capacity or
  // max_freq) of each cpu.
  base::FileEnumerator enumerator(cpu_bus_dir, false /*recursive*/,
                                  base::FileEnumerator::DIRECTORIES);
  std::vector<std::string> small_cpu_ids;
  int num_cpus = 0;

  for (base::FilePath subdir = enumerator.Next(); !subdir.empty();
       subdir = enumerator.Next()) {
    std::string item_str;
    if (base::ReadFileToString(subdir.Append(attribute), &item_str)) {
      std::string subdir_name = subdir.BaseName().value();
      DCHECK_GT(subdir_name.size(), kCpuPrefixSize);

      int item = atoi(item_str.c_str());
      if (item <= 0) {
        LOG(ERROR) << "Invalid value read from " << subdir_name
                   << " attribute file!";
        continue;
      }
      std::string cpu_id = subdir_name.substr(kCpuPrefixSize);

      if (item <= min_item) {
        if (item < min_item) {
          small_cpu_ids.clear();
          min_item = item;
        }
        small_cpu_ids.emplace_back(std::move(cpu_id));
      }
    }
    num_cpus++;
  }

  // If the number of small cpus is not less than num_cpus, the cpu arch is not
  // hybrid, clear the small cpu id list and return;
  if (small_cpu_ids.size() >= num_cpus) {
    small_cpu_ids.clear();
    return small_cpu_ids;
  }

  std::sort(small_cpu_ids.begin(), small_cpu_ids.end());
  return small_cpu_ids;
}

std::vector<std::string> CalculateSmallCoreCpus(
    const base::FilePath& cpu_bus_dir) {
  // Gets small cpu ids through traversing cpu_capacity of each cpu.
  if (auto small_cpu_ids = GetSmallCoreCpuIdsFromAttr(cpu_bus_dir, kCpuCapFile);
      !small_cpu_ids.empty()) {
    return small_cpu_ids;
  }

  // Gets small cpu ids through traversing cpuinfo_max_freq of each cpu.
  if (auto small_cpu_ids =
          GetSmallCoreCpuIdsFromAttr(cpu_bus_dir, kCpuMaxFreqFile);
      !small_cpu_ids.empty()) {
    return small_cpu_ids;
  }

  return {};
}

bool ConfigureNonUrgentCpuset(brillo::CrosConfigInterface* cros_config) {
  base::FilePath nonurgent_path(kCpusetNonUrgentDir);
  if (!base::PathExists(nonurgent_path)) {
    LOG(WARNING) << "The path of non-urgent cpuset doesn't exist!";
    return false;
  }

  std::string cpuset_conf;

  // Writes cpuset-nonurgent to non-urgent cpuset if it's specified in
  // cros_config.
  if (cros_config &&
      cros_config->GetString("/scheduler-tune", "cpuset-nonurgent",
                             &cpuset_conf) &&
      !cpuset_conf.empty()) {
    if (!base::WriteFile(nonurgent_path.Append("cpus"), cpuset_conf)) {
      LOG(ERROR) << "Error writing non urgent cpuset!";
      return false;
    }
    LOG(INFO) << "Non-urgent cpuset is " << cpuset_conf << " from cros_config";
    return true;
  }

  base::FilePath use_flags_file(kUseFlagsFile);
  if (!base::PathExists(use_flags_file)) {
    LOG(INFO) << "The file " << kUseFlagsFile << " doesn't exist, no "
              << "big_little flag, so non-urgent cpuset is all cpus.";
    return false;
  }

  if (!HasHybridFlag(use_flags_file)) {
    LOG(INFO) << "No big_little use flag, non-urgent cpuset is all cpus.";
    return false;
  }

  // Use all small cores as non-urgent cpuset, if cpuset-nonurgent isn't
  // specified in cros_config.
  std::vector<std::string> ecpu_ids =
      CalculateSmallCoreCpus(base::FilePath(kCpuBusDir));
  if (ecpu_ids.empty()) {
    return false;
  }

  std::string ecpu_mask = base::JoinString(ecpu_ids, ",");

  LOG(INFO) << "The board has big_little use flag, non-urgent cpuset is "
            << ecpu_mask << ".";

  if (!base::WriteFile(nonurgent_path.Append("cpus"), ecpu_mask)) {
    LOG(ERROR) << "Error writing mask of small cores to non urgent cpuset!";
    return false;
  }

  return true;
}

}  // namespace login_manager
