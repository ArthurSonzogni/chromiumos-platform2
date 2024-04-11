// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_process_runner.h"

#include <base/strings/string_split.h>

#include "patchpanel/datapath.h"

using testing::_;
using testing::ElementsAreArray;

namespace patchpanel {

namespace {
constexpr char kIpPath[] = "/bin/ip";

std::vector<std::string> SplitArgs(std::string_view args) {
  return base::SplitString(args, " ", base::WhitespaceHandling::TRIM_WHITESPACE,
                           base::SplitResult::SPLIT_WANT_NONEMPTY);
}
}  // namespace

MockProcessRunner::MockProcessRunner() = default;

MockProcessRunner::~MockProcessRunner() = default;

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

}  // namespace patchpanel
