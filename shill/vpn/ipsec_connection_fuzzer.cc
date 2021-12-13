// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

#include <base/logging.h>

#include "shill/process_manager.h"
#include "shill/vpn/ipsec_connection.h"

namespace shill {

class IPsecConnectionUnderTest : public IPsecConnection {
 public:
  // Initialized the IPsecConnection class without an L2TPConnection, and thus
  // the code path for parsing virtual IP can be covered.
  explicit IPsecConnectionUnderTest(ProcessManager* process_manager)
      : IPsecConnection(
            nullptr, nullptr, nullptr, nullptr, nullptr, process_manager) {}

  IPsecConnectionUnderTest(const IPsecConnectionUnderTest&) = delete;
  IPsecConnectionUnderTest& operator=(const IPsecConnectionUnderTest&) = delete;

  void TriggerReadIPsecStatus() {
    IPsecConnection::ScheduleConnectTask(ConnectStep::kIPsecConnected);
  }

  // Do nothing since we only want to test the `swanctl --list-sas` step.
  void ScheduleConnectTask(ConnectStep) override {}
};

class FakeProcessManager : public ProcessManager {
 public:
  explicit FakeProcessManager(const std::string& data) : data_(data) {}
  FakeProcessManager(const FakeProcessManager&) = delete;
  FakeProcessManager& operator=(const FakeProcessManager&) = delete;

  pid_t StartProcessInMinijailWithStdout(
      const base::Location&,
      const base::FilePath&,
      const std::vector<std::string>&,
      const std::map<std::string, std::string>&,
      const MinijailOptions&,
      ExitWithStdoutCallback callback) {
    std::move(callback).Run(/*exit_status=*/0, data_);
    return 123;
  }

 private:
  std::string data_;
};

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FakeProcessManager process_manager(
      std::string{reinterpret_cast<const char*>(data), size});
  IPsecConnectionUnderTest connection(&process_manager);

  connection.TriggerReadIPsecStatus();

  return 0;
}

}  // namespace shill
