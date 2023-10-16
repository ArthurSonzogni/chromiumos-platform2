// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_FAKE_PROCESS_RUNNER_H_
#define PATCHPANEL_FAKE_PROCESS_RUNNER_H_

#include <string>
#include <string_view>
#include <vector>

#include "patchpanel/minijailed_process_runner.h"

namespace patchpanel {

// All commands always succeed.
class FakeProcessRunner : public MinijailedProcessRunner {
 public:
  FakeProcessRunner() = default;
  FakeProcessRunner(const FakeProcessRunner&) = delete;
  FakeProcessRunner& operator=(const FakeProcessRunner&) = delete;

  ~FakeProcessRunner() = default;

  int RunIp(const std::vector<std::string>& argv,
            bool as_patchpanel_user,
            bool log_failures) override {
    return 0;
  }

  int RunIptables(std::string_view iptables_path,
                  Iptables::Table table,
                  Iptables::Command command,
                  std::string_view chain,
                  const std::vector<std::string>& argv,
                  bool log_failures,
                  std::optional<base::TimeDelta> timeout,
                  std::string* output) override {
    return 0;
  }

  int RunIpNetns(const std::vector<std::string>& argv,
                 bool log_failures) override {
    return 0;
  }
};

}  // namespace patchpanel

#endif  // PATCHPANEL_FAKE_PROCESS_RUNNER_H_
