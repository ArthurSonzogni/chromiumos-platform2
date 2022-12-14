// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/slaac_controller.h"

#include <memory>

#include <base/logging.h>

#include "shill/net/ndisc.h"
#include "shill/net/rtnl_handler.h"
#include "shill/net/shill_time.h"
#include "shill/network/network.h"

namespace shill {

SLAACController::SLAACController(int interface_index,
                                 Network* network,
                                 RTNLHandler* rtnl_handler)
    : interface_index_(interface_index),
      network_(network),
      time_(Time::GetInstance()),
      rtnl_handler_(rtnl_handler) {}

SLAACController::~SLAACController() = default;

void SLAACController::StartRTNL() {
  address_listener_ = std::make_unique<RTNLListener>(
      RTNLHandler::kRequestAddr,
      base::BindRepeating(&SLAACController::AddressMsgHandler,
                          weak_factory_.GetWeakPtr()),
      rtnl_handler_);
  rdnss_listener_ = std::make_unique<RTNLListener>(
      RTNLHandler::kRequestRdnss,
      base::BindRepeating(&SLAACController::RDNSSMsgHandler,
                          weak_factory_.GetWeakPtr()),
      rtnl_handler_);
  rtnl_handler_->RequestDump(RTNLHandler::kRequestAddr);
}

void SLAACController::AddressMsgHandler(const RTNLMessage& msg) {}

void SLAACController::RDNSSMsgHandler(const RTNLMessage& msg) {
  DCHECK(msg.type() == RTNLMessage::kTypeRdnss);
  if (msg.interface_index() != interface_index_) {
    return;
  }

  const RTNLMessage::RdnssOption& rdnss_option = msg.rdnss_option();
  ipv6_dns_server_lifetime_seconds_ = rdnss_option.lifetime;
  ipv6_dns_server_addresses_ = rdnss_option.addresses;
  if (!time_->GetSecondsBoottime(&ipv6_dns_server_received_time_seconds_)) {
    LOG(DFATAL) << "GetSecondsBoottime failed.";
    return;
  }

  // Notify device of the IPv6 DNS server addresses update.
  network_->OnIPv6DnsServerAddressesChanged();
}

bool SLAACController::GetIPv6DNSServerAddresses(
    std::vector<IPAddress>* address_list, uint32_t* life_time_seconds) {
  if (ipv6_dns_server_addresses_.empty()) {
    return false;
  }

  // Determine the remaining DNS server life time.
  if (ipv6_dns_server_lifetime_seconds_ == ND_OPT_LIFETIME_INFINITY) {
    *life_time_seconds = ND_OPT_LIFETIME_INFINITY;
  } else {
    time_t cur_time;
    if (!time_->GetSecondsBoottime(&cur_time)) {
      LOG(DFATAL) << "GetSecondsBoottime failed.";
      return false;
    }
    uint32_t time_elapsed = static_cast<uint32_t>(
        cur_time - ipv6_dns_server_received_time_seconds_);
    if (time_elapsed >= ipv6_dns_server_lifetime_seconds_) {
      *life_time_seconds = 0;
    } else {
      *life_time_seconds = ipv6_dns_server_lifetime_seconds_ - time_elapsed;
    }
  }
  *address_list = ipv6_dns_server_addresses_;
  return true;
}

}  // namespace shill
