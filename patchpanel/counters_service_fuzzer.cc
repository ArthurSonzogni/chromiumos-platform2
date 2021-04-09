// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/ioctl.h>

#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "patchpanel/counters_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/firewall.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/system.h"

namespace patchpanel {
namespace {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
  }
  base::AtExitManager at_exit;
};

class FakeProcessRunner : public MinijailedProcessRunner {
 public:
  FakeProcessRunner() = default;
  FakeProcessRunner(const FakeProcessRunner&) = delete;
  FakeProcessRunner& operator=(const FakeProcessRunner&) = delete;
  ~FakeProcessRunner() = default;

  int Run(const std::vector<std::string>& argv, bool log_failures) override {
    return 0;
  }

  int RunSync(const std::vector<std::string>& argv,
              bool log_failures,
              std::string* output) override {
    return 0;
  }
};

// Always succeeds
class NoopSystem : public System {
 public:
  NoopSystem() = default;
  NoopSystem(const NoopSystem&) = delete;
  NoopSystem& operator=(const NoopSystem&) = delete;
  virtual ~NoopSystem() = default;

  int Ioctl(int fd, ioctl_req_t request, const char* argp) override {
    return 0;
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider provider(data, size);
  auto runner = new FakeProcessRunner();
  auto firewall = new Firewall();
  auto system = new NoopSystem();
  Datapath datapath(runner, firewall, system);
  CountersService counters_svc(&datapath);

  while (provider.remaining_bytes() > 0) {
    counters_svc.GetCounters({});
  }

  return 0;
}

}  // namespace
}  // namespace patchpanel
