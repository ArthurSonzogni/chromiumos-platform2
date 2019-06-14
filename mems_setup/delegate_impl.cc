// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/stringprintf.h>

#include "mems_setup/delegate_impl.h"

namespace mems_setup {

namespace {
const char kVpdDataPath[] =
    "/mnt/stateful_partition/unencrypted/cache/vpd/full-v2.txt";
const char kSysModulePath[] = "/sys/module";
}  // namespace

bool LoadVpdFromString(const std::string& vpd_data,
                       std::map<std::string, std::string>* cache) {
  CHECK(cache);

  size_t nl_pos = 0;
  const size_t vpd_len = vpd_data.size();
  while (nl_pos < vpd_len) {
    const auto i_eq = vpd_data.find('=', nl_pos);
    if (i_eq == std::string::npos)
      break;

    auto i_nl = vpd_data.find('\n', i_eq + 1);
    if (i_nl == std::string::npos)
      i_nl = vpd_len;

    // VPD entries come in "key"="value" form, including the quotes;
    // the purpose of the substring operations here is to remove those
    // quotes, as they are be inconvenient to deal with from C++ code.
    const auto key = vpd_data.substr(nl_pos + 1, i_eq - nl_pos - 2);
    const auto value = vpd_data.substr(i_eq + 2, i_nl - i_eq - 3);
    cache->emplace(key, value);

    nl_pos = i_nl + 1;
  }

  return true;
}

void DelegateImpl::LoadVpdIfNeeded() {
  if (vpd_loaded_)
    return;

  std::string vpd_data;
  base::FilePath vpd_path(kVpdDataPath);
  if (!base::ReadFileToString(vpd_path, &vpd_data)) {
    LOG(ERROR) << "failed to read VPD data";
    return;
  }

  vpd_loaded_ = LoadVpdFromString(vpd_data, &vpd_cache_);
}

base::Optional<std::string> DelegateImpl::ReadVpdValue(const std::string& key) {
  LoadVpdIfNeeded();

  auto k = vpd_cache_.find(key);
  if (k != vpd_cache_.end())
    return k->second;
  else
    return base::nullopt;
}

bool DelegateImpl::ProbeKernelModule(const std::string& module) {
  base::FilePath init_path(kSysModulePath);
  init_path = init_path.Append(module).Append("initstate");

  std::string init_data;

  // If we can tell that a module has been loaded, then just return along
  // the happy path instead of forking a new process.
  if (base::ReadFileToString(init_path, &init_data)) {
    if (init_data == "live\n")
      return true;
  }

  std::vector<std::string> argv;
  argv.emplace_back("/sbin/modprobe");
  argv.emplace_back("-q");
  argv.emplace_back(module);

  base::Process process(base::LaunchProcess(argv, base::LaunchOptions()));
  if (!process.IsValid()) {
    LOG(ERROR) << "failed to launch modprobe";
    return false;
  }
  int exit_code = -1;
  if (!process.WaitForExit(&exit_code)) {
    LOG(ERROR) << "modprobe exit could not be detected";
    return false;
  }
  return exit_code == 0;
}

bool DelegateImpl::Exists(const base::FilePath& fp) {
  return base::PathExists(fp);
}

}  // namespace mems_setup
