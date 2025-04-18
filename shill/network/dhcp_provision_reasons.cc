// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcp_provision_reasons.h"

namespace shill {

std::string DHCPProvisionReasonToMetricString(DHCPProvisionReason reason) {
  switch (reason) {
    case DHCPProvisionReason::kConnect:
      return "Connect";
    case DHCPProvisionReason::kLeaseExpiration:
      return "LeaseExpiration";
    case DHCPProvisionReason::kRoaming:
      return "Roaming";
    case DHCPProvisionReason::kSuspendResume:
      return "SuspendResume";
  }
}

}  // namespace shill
