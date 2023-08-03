// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/slaac_controller.h"

#include <linux/rtnetlink.h>
#include <netinet/icmp6.h>
#include <sys/socket.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <base/logging.h>
#include <net-base/ip_address.h>

#include "shill/net/ndisc.h"
#include "shill/net/rtnl_handler.h"

namespace shill {

SLAACController::SLAACController(int interface_index,
                                 ProcFsStub* proc_fs,
                                 RTNLHandler* rtnl_handler,
                                 EventDispatcher* dispatcher)
    : interface_index_(interface_index),
      proc_fs_(proc_fs),
      rtnl_handler_(rtnl_handler),
      dispatcher_(dispatcher) {}

SLAACController::~SLAACController() = default;

void SLAACController::Start(
    std::optional<net_base::IPv6Address> link_local_address) {
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

  link_local_address_ = link_local_address;

  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6, ProcFsStub::kIPFlagUseTempAddr,
                      ProcFsStub::kIPFlagUseTempAddrUsedAndDefault);
  proc_fs_->SetIPFlag(
      net_base::IPFamily::kIPv6,
      ProcFsStub::kIPFlagAcceptDuplicateAddressDetection,
      ProcFsStub::kIPFlagAcceptDuplicateAddressDetectionEnabled);

  // Temporarily disable IPv6 to remove all existing addresses.
  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6, ProcFsStub::kIPFlagDisableIPv6,
                      "1");
  if (link_local_address_) {
    // Temporarily disable accepting RA so we get a time to configure link local
    // address.
    proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                        ProcFsStub::kIPFlagAcceptRouterAdvertisements,
                        ProcFsStub::kIPFlagAcceptRouterAdvertisementsNever);
  } else {
    proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                        ProcFsStub::kIPFlagAcceptRouterAdvertisements,
                        ProcFsStub::kIPFlagAcceptRouterAdvertisementsAlways);
  }

  // Re-enable IPv6.
  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6, ProcFsStub::kIPFlagDisableIPv6,
                      "0");
  if (link_local_address_) {
    ConfigureLinkLocalAddress();
    proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                        ProcFsStub::kIPFlagAcceptRouterAdvertisements,
                        ProcFsStub::kIPFlagAcceptRouterAdvertisementsAlways);
  }
  // Turning on accept_ra does not force sending RS so we need to do it
  // ourselves.
  SendRouterSolicitation();
}

void SLAACController::RegisterCallback(UpdateCallback update_callback) {
  update_callback_ = update_callback;
}

void SLAACController::Stop() {
  StopRDNSSTimer();
  address_listener_.reset();
  rdnss_listener_.reset();
}

void SLAACController::AddressMsgHandler(const RTNLMessage& msg) {
  DCHECK(msg.type() == RTNLMessage::kTypeAddress);
  if (msg.interface_index() != interface_index_) {
    return;
  }

  const RTNLMessage::AddressStatus& status = msg.address_status();
  if (msg.family() != AF_INET6 || status.scope != RT_SCOPE_UNIVERSE ||
      (status.flags & IFA_F_PERMANENT)) {
    // SLAACController only monitors IPv6 global address that is not PERMANENT.
    return;
  }

  const auto addr_bytes = msg.HasAttribute(IFA_LOCAL)
                              ? msg.GetAttribute(IFA_LOCAL)
                              : msg.GetAttribute(IFA_ADDRESS);
  const auto ipv6_cidr = net_base::IPv6CIDR::CreateFromBytesAndPrefix(
      addr_bytes, status.prefix_len);
  if (!ipv6_cidr) {
    LOG(ERROR) << "Failed to create IPv6CIDR: address length="
               << addr_bytes.size() << ", prefix length=" << status.prefix_len;
    return;
  }

  const auto iter = std::find_if(
      slaac_addresses_.begin(), slaac_addresses_.end(),
      [&](const AddressData& data) { return data.cidr == *ipv6_cidr; });
  if (iter != slaac_addresses_.end()) {
    if (msg.mode() == RTNLMessage::kModeDelete) {
      LOG(INFO) << "RTNL cache: Delete address " << ipv6_cidr->ToString()
                << " for interface " << interface_index_;
      slaac_addresses_.erase(iter);
    } else {
      iter->flags = status.flags;
      iter->scope = status.scope;
    }
  } else {
    if (msg.mode() == RTNLMessage::kModeAdd) {
      LOG(INFO) << "RTNL cache: Add address " << ipv6_cidr->ToString()
                << " for interface " << interface_index_;
      slaac_addresses_.insert(
          slaac_addresses_.begin(),
          AddressData(*ipv6_cidr, status.flags, status.scope));
    } else if (msg.mode() == RTNLMessage::kModeDelete) {
      LOG(WARNING) << "RTNL cache: Deleting non-cached address "
                   << ipv6_cidr->ToString() << " for interface "
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
  uint32_t rdnss_lifetime_seconds = rdnss_option.lifetime;
  rdnss_addresses_ = rdnss_option.addresses;

  // Stop any existing timer.
  StopRDNSSTimer();

  if (rdnss_lifetime_seconds == 0) {
    rdnss_addresses_.clear();
  } else if (rdnss_lifetime_seconds != ND_OPT_LIFETIME_INFINITY) {
    // Setup timer to monitor DNS server lifetime if not infinite lifetime.
    base::TimeDelta delay = base::Seconds(rdnss_lifetime_seconds);
    StartRDNSSTimer(delay);
  }

  if (update_callback_) {
    update_callback_.Run(UpdateType::kRDNSS);
  }
}

void SLAACController::StartRDNSSTimer(base::TimeDelta delay) {
  rdnss_expired_callback_.Reset(base::BindOnce(&SLAACController::RDNSSExpired,
                                               weak_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, rdnss_expired_callback_.callback(),
                               delay);
}

void SLAACController::StopRDNSSTimer() {
  rdnss_expired_callback_.Cancel();
}

void SLAACController::RDNSSExpired() {
  rdnss_addresses_.clear();
  if (update_callback_) {
    update_callback_.Run(UpdateType::kRDNSS);
  }
}

std::vector<net_base::IPv6CIDR> SLAACController::GetAddresses() const {
  std::vector<net_base::IPv6CIDR> result;
  for (const auto& address_data : slaac_addresses_) {
    result.push_back(address_data.cidr);
  }
  return result;
}

std::vector<net_base::IPv6Address> SLAACController::GetRDNSSAddresses() const {
  return rdnss_addresses_;
}

void SLAACController::ConfigureLinkLocalAddress() {
  if (!link_local_address_) {
    return;
  }
  const auto link_local_mask =
      *net_base::IPv6CIDR::CreateFromStringAndPrefix("fe80::", 10);
  if (!link_local_mask.InSameSubnetWith(*link_local_address_)) {
    LOG(WARNING) << "interface " << interface_index_ << ": Address "
                 << *link_local_address_ << " is not a link local address";
    return;
  }
  LOG(INFO) << "interface " << interface_index_
            << ": configuring link local address " << *link_local_address_;
  rtnl_handler_->AddInterfaceAddress(
      interface_index_,
      net_base::IPCIDR(*net_base::IPv6CIDR::CreateFromAddressAndPrefix(
          *link_local_address_, 64)),
      std::nullopt);
}

void SLAACController::SendRouterSolicitation() {
  auto sockfd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
  struct sockaddr_in6 src_addr;
  bzero(&src_addr, sizeof(src_addr));
  src_addr.sin6_family = AF_INET6;
  src_addr.sin6_scope_id = interface_index_;
  if (link_local_address_) {
    src_addr.sin6_addr = link_local_address_->ToIn6Addr();
  }
  if (bind(sockfd, reinterpret_cast<sockaddr*>(&src_addr), sizeof(src_addr)) <
      0) {
    PLOG(WARNING) << "interface " << interface_index_
                  << ": Error binding address for sending RS";
  }

  struct sockaddr_in6 dst_addr;
  bzero(&dst_addr, sizeof(dst_addr));
  dst_addr.sin6_addr = {
      {{0xff, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}}};  // ff02::2

  nd_router_solicit packet;
  bzero(&packet, sizeof(packet));
  packet.nd_rs_hdr.icmp6_type = ND_ROUTER_SOLICIT;

  // b/294334471: Define maximum hop limit for the packet.
  constexpr int kIPv6MaxHopLimit = 255;
  if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &kIPv6MaxHopLimit,
                 sizeof(kIPv6MaxHopLimit)) < 0) {
    PLOG(WARNING) << "interface " << interface_index_
                  << ": Error configuring hop limit in RS.";
  }

  if (sendto(sockfd, &packet, sizeof(packet), 0,
             reinterpret_cast<sockaddr*>(&dst_addr), sizeof(dst_addr)) < 0) {
    PLOG(WARNING) << "interface " << interface_index_ << ": Error sending RS.";
  }
}

}  // namespace shill
