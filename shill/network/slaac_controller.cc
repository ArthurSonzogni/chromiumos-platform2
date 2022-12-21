// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/slaac_controller.h"

#include <linux/rtnetlink.h>

#include <memory>
#include <utility>

#include <base/logging.h>

#include "shill/net/ip_address.h"
#include "shill/net/ndisc.h"
#include "shill/net/rtnl_handler.h"
#include "shill/net/shill_time.h"

namespace shill {

SLAACController::SLAACController(int interface_index, RTNLHandler* rtnl_handler)
    : interface_index_(interface_index),
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

void SLAACController::RegisterCallback(UpdateCallback update_callback) {
  update_callback_ = update_callback;
}

void SLAACController::AddressMsgHandler(const RTNLMessage& msg) {
  DCHECK(msg.type() == RTNLMessage::kTypeAddress);
  if (msg.interface_index() != interface_index_) {
    return;
  }
  const RTNLMessage::AddressStatus& status = msg.address_status();
  IPAddress address(msg.family(),
                    msg.HasAttribute(IFA_LOCAL) ? msg.GetAttribute(IFA_LOCAL)
                                                : msg.GetAttribute(IFA_ADDRESS),
                    status.prefix_len);

  if (address.family() != IPAddress::kFamilyIPv6 ||
      status.scope != RT_SCOPE_UNIVERSE || (status.flags & IFA_F_PERMANENT)) {
    // SLAACController only monitors IPv6 global address that is not PERMANENT.
    return;
  }

  std::vector<AddressData>::iterator iter;
  for (iter = slaac_addresses_.begin(); iter != slaac_addresses_.end();
       ++iter) {
    if (address.Equals(iter->address)) {
      break;
    }
  }
  if (iter != slaac_addresses_.end()) {
    if (msg.mode() == RTNLMessage::kModeDelete) {
      LOG(INFO) << "RTNL cache: Delete address " << address.ToString()
                << " for interface " << interface_index_;
      slaac_addresses_.erase(iter);
    } else {
      iter->flags = status.flags;
      iter->scope = status.scope;
    }
  } else {
    if (msg.mode() == RTNLMessage::kModeAdd) {
      LOG(INFO) << "RTNL cache: Add address " << address.ToString()
                << " for interface " << interface_index_;
      slaac_addresses_.insert(
          slaac_addresses_.begin(),
          AddressData(std::move(address), status.flags, status.scope));
    } else if (msg.mode() == RTNLMessage::kModeDelete) {
      LOG(WARNING) << "RTNL cache: Deleting non-cached address "
                   << address.ToString() << " for interface "
                   << interface_index_;
    }
  }

  // Sort slaac_addresses_ to match the kernel's preference so the primary
  // address always comes at top. Note that this order is based on the premise
  // that we set net.ipv6.conf.use_tempaddr = 2.
  static struct {
    bool operator()(const AddressData& a, const AddressData& b) const {
      // Prefer non-deprecated addresses to deprecated addresses to match the
      // kernel's preference.
      if (!(a.flags & IFA_F_DEPRECATED) && (b.flags & IFA_F_DEPRECATED)) {
        return true;
      }
      if (!(b.flags & IFA_F_DEPRECATED) && (a.flags & IFA_F_DEPRECATED)) {
        return false;
      }
      // Prefer temporary addresses to non-temporary addresses to match the
      // kernel's preference.
      if ((a.flags & IFA_F_TEMPORARY) && !(b.flags & IFA_F_TEMPORARY)) {
        return true;
      }
      if ((b.flags & IFA_F_TEMPORARY) && !(a.flags & IFA_F_TEMPORARY)) {
        return false;
      }
      return false;
    }
  } address_preference;
  std::stable_sort(slaac_addresses_.begin(), slaac_addresses_.end(),
                   address_preference);

  if (update_callback_) {
    update_callback_.Run(UpdateType::kAddress);
  }
}

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

  if (update_callback_) {
    update_callback_.Run(UpdateType::kRDNSS);
  }
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

std::vector<IPAddress> SLAACController::GetAddresses() const {
  std::vector<IPAddress> result;
  for (const auto& address_data : slaac_addresses_) {
    result.push_back(address_data.address);
  }
  return result;
}

}  // namespace shill
