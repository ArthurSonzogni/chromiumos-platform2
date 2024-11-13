// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_FAKE_STARTUP_DEP_IMPL_H_
#define INIT_STARTUP_FAKE_STARTUP_DEP_IMPL_H_

#include <stdlib.h>
#include <sys/types.h>

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/file_util.h>

#include "init/startup/startup_dep_impl.h"

namespace startup {

// This class is utilized by tests to override functions that
// use of system calls or other functionality that utilizes
// system command outputs.

class FakeStartupDep : public StartupDep {
 public:
  explicit FakeStartupDep(libstorage::Platform* platform);

  int GetBootAlertForArg(const std::string& arg);

  void GetClobberLog(std::string* log);

  std::set<std::string> GetClobberArgs();

  void BootAlert(const std::string& arg) override;

  void ClobberLog(const std::string& msg) override;

  void Clobber(const std::vector<std::string>& args) override;

 private:
  std::unordered_map<std::string, int> alert_result_map_;
  std::string clobber_log_;
  std::set<std::string> clobber_args_;
};

}  // namespace startup

#endif  // INIT_STARTUP_FAKE_STARTUP_DEP_IMPL_H_
