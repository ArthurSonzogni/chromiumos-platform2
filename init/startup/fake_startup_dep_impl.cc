// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/fake_startup_dep_impl.h"

#include <stdlib.h>
#include <sys/types.h>

#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <libstorage/platform/platform.h>

namespace startup {

FakeStartupDep::FakeStartupDep(libstorage::Platform* platform)
    : StartupDep(platform), platform_(platform) {}

int FakeStartupDep::GetBootAlertForArg(const std::string& arg) {
  return alert_result_map_[arg];
}

void FakeStartupDep::GetClobberLog(std::string* log) {
  *log = clobber_log_;
}

std::set<std::string> FakeStartupDep::GetClobberArgs() {
  return clobber_args_;
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
  clobber_log_ = msg;
}

void FakeStartupDep::Clobber(const std::vector<std::string>& args) {
  for (std::string arg : args) {
    clobber_args_.insert(arg);
  }
}

}  // namespace startup
