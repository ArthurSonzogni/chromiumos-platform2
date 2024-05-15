// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/types.h>

#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <libstorage/platform/platform.h>

#include "init/startup/fake_startup_dep_impl.h"

namespace startup {

FakeStartupDep::FakeStartupDep(libstorage::Platform* platform)
    : StartupDep(), platform_(platform) {}

void FakeStartupDep::SetMountEncOutputForArg(const std::string& arg,
                                             const std::string& output) {
  mount_enc_result_map_[arg] = output;
}

int FakeStartupDep::GetBootAlertForArg(const std::string& arg) {
  return alert_result_map_[arg];
}

void FakeStartupDep::SetClobberLogFile(const base::FilePath& path) {
  clobber_log_ = path;
}

std::set<std::string> FakeStartupDep::GetClobberArgs() {
  return clobber_args_;
}

int FakeStartupDep::MountEncrypted(const std::vector<std::string>& args,
                                   std::string* output) {
  std::string arg;
  if (!args.empty()) {
    arg = args.at(0);
  }
  if (mount_enc_result_map_.count(arg) == 0) {
    return -1;
  }

  *output = mount_enc_result_map_[arg];
  return 0;
}

void FakeStartupDep::BootAlert(const std::string& arg) {
  alert_result_map_[arg] = 1;
}

void FakeStartupDep::RemoveInBackground(
    const std::vector<base::FilePath>& paths) {
  for (auto path : paths) {
    platform_->DeletePathRecursively(path);
  }
}

void FakeStartupDep::ClobberLog(const std::string& msg) {
  platform_->WriteStringToFile(clobber_log_, msg);
}

void FakeStartupDep::Clobber(const std::vector<std::string>& args) {
  for (std::string arg : args) {
    clobber_args_.insert(arg);
  }
}

}  // namespace startup
