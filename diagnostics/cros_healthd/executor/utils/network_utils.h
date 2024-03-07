// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_NETWORK_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_NETWORK_UTILS_H_

#include <string>

namespace diagnostics {

bool IsValidWirelessInterfaceName(const std::string& interface_name);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_NETWORK_UTILS_H_
