// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcp_client_proxy.h"

#include <sstream>
#include <string>
#include <string_view>

#include <base/functional/callback.h>

namespace shill {

DHCPClientProxy::DHCPClientProxy(std::string_view interface,
                                 EventHandler* handler)
    : interface_(interface), handler_(handler) {}

DHCPClientProxy::~DHCPClientProxy() = default;

std::string DHCPClientProxy::Options::ToString() const {
  std::ostringstream oss;
  oss << "{";
  oss << "use_legacy_dhcpcd=" << use_legacy_dhcpcd << ", ";
  oss << "use_arp_gateway=" << use_arp_gateway << ", ";
  oss << "use_rfc_8925=" << use_rfc_8925 << ", ";
  oss << "apply_dscp=" << apply_dscp;
  // Skip hostname since it may contain PII.
  oss << "}";
  return oss.str();
}

bool operator==(const DHCPClientProxy::Options&,
                const DHCPClientProxy::Options&) = default;

void DHCPClientProxy::OnProcessExited(int pid, int exit_status) {
  handler_->OnProcessExited(pid, exit_status);
}

bool DHCPClientProxy::NeedConfiguration(DHCPClientProxy::EventReason reason) {
  switch (reason) {
    case DHCPClientProxy::EventReason::kBound:
    case DHCPClientProxy::EventReason::kRebind:
    case DHCPClientProxy::EventReason::kReboot:
    case DHCPClientProxy::EventReason::kRenew:
    case DHCPClientProxy::EventReason::kGatewayArp:
    case DHCPClientProxy::EventReason::kBound6:
    case DHCPClientProxy::EventReason::kRebind6:
    case DHCPClientProxy::EventReason::kReboot6:
    case DHCPClientProxy::EventReason::kRenew6:
      return true;

    case DHCPClientProxy::EventReason::kFail:
    case DHCPClientProxy::EventReason::kNak:
    case DHCPClientProxy::EventReason::kIPv6OnlyPreferred:
      return false;
  }
}

}  // namespace shill
