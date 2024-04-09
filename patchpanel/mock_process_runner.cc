// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_process_runner.h"

#include <base/strings/string_split.h>

#include "patchpanel/datapath.h"

using testing::_;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;

namespace patchpanel {

namespace {
constexpr char kIpPath[] = "/bin/ip";
constexpr char kIptablesPath[] = "/sbin/iptables";
constexpr char kIp6tablesPath[] = "/sbin/ip6tables";

std::vector<std::string> SplitArgs(std::string_view args) {
  return base::SplitString(args, " ", base::WhitespaceHandling::TRIM_WHITESPACE,
                           base::SplitResult::SPLIT_WANT_NONEMPTY);
}
}  // namespace

MockProcessRunner::MockProcessRunner() = default;

MockProcessRunner::~MockProcessRunner() = default;

std::unique_ptr<MinijailedProcessRunner::ScopedIptablesBatchMode>
MockProcessRunner::AcquireIptablesBatchMode() {
  return nullptr;
}

void MockProcessRunner::ExpectCallIp(IpFamily family, std::string_view argv) {
  std::vector<std::string_view> call_args;
  if (family == IpFamily::kIPv4) {
    call_args = {kIpPath};
  } else if (family == IpFamily::kIPv6) {
    call_args = {kIpPath, "-6"};
  } else {
    FAIL() << "IP family is invalid, only IPv4 or IPv6 is supported: "
           << family;
  }
  auto args = SplitArgs(argv);
  call_args.insert(call_args.end(), args.begin(), args.end());
  EXPECT_CALL(*this, RunIp(ElementsAreArray(call_args), _, _));
}

void MockProcessRunner::ExpectNoCallIp() {
  EXPECT_CALL(*this, RunIp).Times(0);
}

void MockProcessRunner::ExpectCallIptables(IpFamily family,
                                           std::string_view argv,
                                           int call_times,
                                           const std::string& output,
                                           bool empty_chain,
                                           int return_value) {
  auto args = SplitArgs(argv);
  const auto table = *Iptables::TableFromName(args[0]);
  const auto command = *Iptables::CommandFromName(args[1]);

  std::string chain = "";
  if (!empty_chain) {
    chain = args[2];
    args.erase(args.begin());
  }
  args.erase(args.begin());
  args.erase(args.begin());
  if (family == IpFamily::kIPv4 || family == IpFamily::kDual) {
    EXPECT_CALL(*this, RunIptables(StrEq(kIptablesPath), table, command,
                                   StrEq(chain), ElementsAreArray(args), _, _))
        .Times(call_times)
        .WillOnce(
            DoAll(testing::WithArgs<6>(testing::Invoke([&](std::string* ptr) {
                    if (ptr != nullptr && !output.empty()) {
                      *ptr = output;
                    }
                  })),
                  Return(return_value)));
  }
  if (family == IpFamily::kIPv6 || family == IpFamily::kDual) {
    EXPECT_CALL(*this, RunIptables(StrEq(kIp6tablesPath), table, command,
                                   StrEq(chain), ElementsAreArray(args), _, _))
        .Times(call_times)
        .WillOnce(
            DoAll(testing::WithArgs<6>(testing::Invoke([&](std::string* ptr) {
                    if (ptr != nullptr && !output.empty()) {
                      *ptr = output;
                    }
                  })),
                  Return(return_value)));
  }
}

void MockProcessRunner::ExpectNoCallIptables(IpFamily family) {
  if (family == IpFamily::kIPv4 || family == IpFamily::kDual) {
    EXPECT_CALL(*this, RunIptables(StrEq(kIptablesPath), _, _, _, _, _, _))
        .Times(0);
  }
  if (family == IpFamily::kIPv6 || family == IpFamily::kDual) {
    EXPECT_CALL(*this, RunIptables(StrEq(kIp6tablesPath), _, _, _, _, _, _))
        .Times(0);
  }
}

}  // namespace patchpanel
