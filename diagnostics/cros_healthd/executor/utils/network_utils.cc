// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/network_utils.h"

#include <re2/re2.h>

namespace diagnostics {

namespace {
// wireless interface name start with "wl" or "ml" and end it with a number. All
// characters are in lowercase.  Max length is 16 characters.
constexpr auto kWirelessInterfaceRegex = R"(([wm]l[a-z][a-z0-9]{1,12}[0-9]))";
}  // namespace

bool IsValidWirelessInterfaceName(const std::string& interface_name) {
  return (RE2::FullMatch(interface_name, kWirelessInterfaceRegex));
}

}  // namespace diagnostics
