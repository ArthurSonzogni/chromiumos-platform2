// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_PROCESS_RUNNER_H_
#define PATCHPANEL_MOCK_PROCESS_RUNNER_H_

#include "patchpanel/minijailed_process_runner.h"

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "patchpanel/datapath.h"

using testing::_;
using testing::ElementsAreArray;

namespace patchpanel {

// This class mocks RunIp() and RunIptables(), and utilizes them to:
// 1. Verify specific calls for ip(), ip6() by ExpectCallIp(), and verify no
// calls by ExpectNoCallIp().
// 2. Verify specific calls for iptables(), ip6tables() by
// ExpectCallIptables(), and verify no calls by ExpectNoCallIptables().
class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner();
  ~MockProcessRunner();

  // Override to be a noop. This is required for RunIptables() to be called
  // instantly instead of being called in batch.
  std::unique_ptr<ScopedIptablesBatchMode> AcquireIptablesBatchMode() override;

  // Sets expectations that `ip()` and `ip6()` is called with the given |argv|.
  void ExpectCallIp(IpFamily family, std::string_view argv);
  // Checks that `ip()` and `ip6()` is not called.
  void ExpectNoCallIp();
  // Sets expectations that `iptables()` and `ip6tables()` is called with the
  // given |argv|, for exactly |call_times| times, and whether |argv| contains
  // chain name is set by |empty_chain|.
  // As a result, output is set with |output|, and returns |return_value|.
  void ExpectCallIptables(IpFamily family,
                          std::string_view argv,
                          int call_times = 1,
                          const std::string& output = "",
                          bool empty_chain = false,
                          int return_value = 0);
  // Checks that `iptables()` and `ip6tables()` is not called for given IP
  // family.
  void ExpectNoCallIptables(IpFamily family = IpFamily::kDual);

  MOCK_METHOD(int,
              RunIp,
              (base::span<std::string_view> argv,
               bool as_patchpanel_user,
               bool log_failures),
              (override));
  MOCK_METHOD(int,
              RunIptables,
              (std::string_view iptables_path,
               Iptables::Table table,
               Iptables::Command command,
               std::string_view chain,
               base::span<std::string_view> argv,
               bool log_failures,
               std::string* output),
              (override));
  MOCK_METHOD(int,
              ip_netns_add,
              (std::string_view netns_name, bool log_failures),
              (override));
  MOCK_METHOD(int,
              ip_netns_attach,
              (std::string_view netns_name, pid_t netns_pid, bool log_failures),
              (override));
  MOCK_METHOD(int,
              ip_netns_delete,
              (std::string_view netns_name, bool log_failures),
              (override));
  MOCK_METHOD(int,
              modprobe_all,
              (base::span<const std::string> modules, bool log_failures),
              (override));
  MOCK_METHOD(int,
              conntrack,
              (std::string_view command,
               base::span<const std::string> argv,
               bool log_failures),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_PROCESS_RUNNER_H_
