// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_PROCESS_RUNNER_H_
#define PATCHPANEL_MOCK_PROCESS_RUNNER_H_

#include "patchpanel/minijailed_process_runner.h"

#include <vector>
#include <string>

#include <gmock/gmock.h>

#include "patchpanel/datapath.h"

namespace patchpanel {

class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner();
  ~MockProcessRunner();

  // Sets expectations that `ip()` and `ip6()` is called with the given |argv|.
  void ExpectCallIp(IpFamily family, std::string_view argv);
  // Checks that `ip()` and `ip6()` is not called.
  void ExpectNoCallIp();

  MOCK_METHOD(int,
              RunIp,
              (base::span<std::string_view> argv,
               bool as_patchpanel_user,
               bool log_failures),
              (override));
  MOCK_METHOD(int,
              iptables,
              (Iptables::Table table,
               Iptables::Command command,
               std::string_view chain,
               const std::vector<std::string>& argv,
               bool log_failures,
               std::string* output),
              (override));
  MOCK_METHOD(int,
              ip6tables,
              (Iptables::Table table,
               Iptables::Command command,
               std::string_view chain,
               const std::vector<std::string>& argv,
               bool log_failures,
               std::string* output),
              (override));
  MOCK_METHOD(int,
              ip_netns_add,
              (const std::string& netns_name, bool log_failures),
              (override));
  MOCK_METHOD(int,
              ip_netns_attach,
              (const std::string& netns_name,
               pid_t netns_pid,
               bool log_failures),
              (override));
  MOCK_METHOD(int,
              ip_netns_delete,
              (const std::string& netns_name, bool log_failures),
              (override));
  MOCK_METHOD(int,
              modprobe_all,
              (const std::vector<std::string>& modules, bool log_failures),
              (override));
  MOCK_METHOD(int,
              conntrack,
              (std::string_view command,
               const std::vector<std::string>& argv,
               bool log_failures),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_PROCESS_RUNNER_H_
