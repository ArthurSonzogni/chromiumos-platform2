// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/slaac_controller.h"

#include <linux/rtnetlink.h>
#include <netinet/icmp6.h>
#include <sys/socket.h>

#include <algorithm>
#include <memory>
#include <optional>

#include <base/logging.h>
#include <base/time/time.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/proc_fs_stub.h>
#include <chromeos/net-base/rtnl_message.h>
#include <metrics/timer.h>

namespace shill {

namespace {
constexpr base::TimeDelta kNoPrefixTimeOut = base::Seconds(2);
}

// Infinity lifetime, defined in rfc8106, section-5.1.
#define ND_OPT_LIFETIME_INFINITY 0xFFFFFFFF

SLAACController::SLAACController(int interface_index,
                                 net_base::ProcFsStub* proc_fs,
                                 net_base::RTNLHandler* rtnl_handler,
                                 EventDispatcher* dispatcher,
                                 std::string_view logging_tag)
    : interface_index_(interface_index),
      proc_fs_(proc_fs),
      rtnl_handler_(rtnl_handler),
      dispatcher_(dispatcher),
      logging_tag_(logging_tag) {}

SLAACController::~SLAACController() = default;

void SLAACController::Start(
    std::optional<net_base::IPv6Address> link_local_address) {
  last_provision_timer_ = std::make_unique<chromeos_metrics::Timer>();
  last_provision_timer_->Start();

  address_listener_ = std::make_unique<net_base::RTNLListener>(
      net_base::RTNLHandler::kRequestAddr,
      base::BindRepeating(&SLAACController::AddressMsgHandler,
                          weak_factory_.GetWeakPtr()),
      rtnl_handler_);
  route_listener_ = std::make_unique<net_base::RTNLListener>(
      net_base::RTNLHandler::kRequestRoute,
      base::BindRepeating(&SLAACController::RouteMsgHandler,
                          weak_factory_.GetWeakPtr()),
      rtnl_handler_);
  nd_option_listener_ = std::make_unique<net_base::RTNLListener>(
      net_base::RTNLHandler::kRequestNdUserOption,
      base::BindRepeating(&SLAACController::NDOptionMsgHandler,
                          weak_factory_.GetWeakPtr()),
      rtnl_handler_);

  link_local_address_ = link_local_address;

  proc_fs_->SetIPFlag(
      net_base::IPFamily::kIPv6,
      net_base::ProcFsStub::kIPFlagPerDeviceRoutingTableForRA,
      net_base::ProcFsStub::kIPFlagPerDeviceRoutingTableForRAEnabled);

  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                      net_base::ProcFsStub::kIPFlagUseTempAddr,
                      net_base::ProcFsStub::kIPFlagUseTempAddrUsedAndDefault);
  proc_fs_->SetIPFlag(
      net_base::IPFamily::kIPv6,
      net_base::ProcFsStub::kIPFlagAcceptDuplicateAddressDetection,
      net_base::ProcFsStub::kIPFlagAcceptDuplicateAddressDetectionEnabled);
  proc_fs_->SetIPFlag(
      net_base::IPFamily::kIPv6,
      net_base::ProcFsStub::kIPFlagAcceptRouterAdvertisements,
      net_base::ProcFsStub::kIPFlagAcceptRouterAdvertisementsAlways);
  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                      net_base::ProcFsStub::kIPFlagHonorPFlag,
                      net_base::ProcFsStub::kIPFlagHonorPFlagEnabled);

  // Temporarily disable IPv6 to remove all existing addresses.
  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                      net_base::ProcFsStub::kIPFlagDisableIPv6, "1");
  // If link local address is specified, don't let kernel generate another one.
  proc_fs_->SetIPFlag(
      net_base::IPFamily::kIPv6,
      net_base::ProcFsStub::kIPFlagAddressGenerationMode,
      link_local_address_
          ? net_base::ProcFsStub::kIPFlagAddressGenerationModeNoLinkLocal
          : net_base::ProcFsStub::kIPFlagAddressGenerationModeDefault);

  // Re-enable IPv6. If kIPFlagAddressGenerationMode is Default, kernel will
  // start SLAAC upon this. If it is NoLinkLocal, kernel will start SLAAC as
  // soon as we add the link local address manually.
  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                      net_base::ProcFsStub::kIPFlagDisableIPv6, "0");
  if (link_local_address_) {
    ConfigureLinkLocalAddress();
  }
}

void SLAACController::RegisterCallback(UpdateCallback update_callback) {
  update_callback_ = update_callback;
}

void SLAACController::Stop() {
  StopRDNSSTimer();
  StopNoPrefixTimer();
  address_listener_.reset();
  nd_option_listener_.reset();
  last_provision_timer_.reset();
  proc_fs_->SetIPFlag(
      net_base::IPFamily::kIPv6,
      net_base::ProcFsStub::kIPFlagPerDeviceRoutingTableForRA,
      net_base::ProcFsStub::kIPFlagPerDeviceRoutingTableForRADisabled);
}

void SLAACController::AddressMsgHandler(const net_base::RTNLMessage& msg) {
  DCHECK(msg.type() == net_base::RTNLMessage::kTypeAddress);
  if (msg.interface_index() != interface_index_) {
    return;
  }

  const net_base::RTNLMessage::AddressStatus& status = msg.address_status();
  if (msg.family() != AF_INET6 || status.scope != RT_SCOPE_UNIVERSE ||
      (status.flags & IFA_F_PERMANENT)) {
    // SLAACController only monitors IPv6 global address that is not PERMANENT.
    return;
  }

  const std::optional<net_base::IPCIDR> cidr = msg.GetAddress();
  if (!cidr || cidr->GetFamily() != net_base::IPFamily::kIPv6) {
    LOG(ERROR) << logging_tag_ << " " << __func__
               << ": RTNLMessage does not have a valid IPv6 address";
    return;
  }
  const auto ipv6_cidr = cidr->ToIPv6CIDR().value();

  // Only record the duration once. Note that Stop() has no effect if the timer
  // has already stopped.
  if (last_provision_timer_) {
    last_provision_timer_->Stop();
  }

  const auto iter = std::find_if(
      slaac_addresses_.begin(), slaac_addresses_.end(),
      [&](const AddressData& data) { return data.cidr == ipv6_cidr; });
  if (iter != slaac_addresses_.end()) {
    if (msg.mode() == net_base::RTNLMessage::kModeDelete) {
      LOG(INFO) << logging_tag_ << " " << __func__
                << ": RTNL cache: Delete address " << ipv6_cidr.ToString();
      slaac_addresses_.erase(iter);
    } else {
      iter->flags = status.flags;
      iter->scope = status.scope;
    }
  } else {
    if (msg.mode() == net_base::RTNLMessage::kModeAdd) {
      LOG(INFO) << logging_tag_ << " " << __func__
                << ": RTNL cache: Add address " << ipv6_cidr.ToString();
      slaac_addresses_.insert(
          slaac_addresses_.begin(),
          AddressData(ipv6_cidr, status.flags, status.scope));
    } else if (msg.mode() == net_base::RTNLMessage::kModeDelete) {
      // This can happen if a fresh SLAACController instance receives
      // RTM_DELADDR msgs pertaining to a Network session which has terminated
      // shortly before.
      LOG(INFO) << logging_tag_ << " " << __func__
                << ": RTNL cache: Deleting non-cached address "
                << ipv6_cidr.ToString();
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

  std::vector<net_base::IPv6CIDR> addresses;
  for (const auto& address_data : slaac_addresses_) {
    addresses.push_back(address_data.cidr);
  }
  if (network_config_.ipv6_addresses == addresses) {
    return;
  }
  network_config_.ipv6_addresses = addresses;

  if (update_callback_) {
    update_callback_.Run(UpdateType::kAddress);
  }
}

void SLAACController::RouteMsgHandler(const net_base::RTNLMessage& msg) {
  DCHECK(msg.type() == net_base::RTNLMessage::kTypeRoute);
  // We only care about IPv6 default route of type RA that routes to
  // |interface_index_|.
  if (!msg.HasAttribute(RTA_OIF)) {
    return;
  }
  if (net_base::byte_utils::FromBytes<int32_t>(msg.GetAttribute(RTA_OIF)) !=
      interface_index_) {
    return;
  }
  const net_base::RTNLMessage::RouteStatus& route_status = msg.route_status();
  if (route_status.type != RTN_UNICAST || route_status.protocol != RTPROT_RA) {
    return;
  }
  if (net_base::FromSAFamily(msg.family()) != net_base::IPFamily::kIPv6) {
    return;
  }
  const auto dst = msg.GetRtaDst();
  if (dst && !dst->IsDefault()) {
    return;
  }
  const auto gateway = msg.GetRtaGateway();
  if (!gateway) {
    LOG(WARNING) << logging_tag_ << " " << __func__
                 << ": IPv6 default route without a gateway";
    return;
  }
  const auto old_gateway = network_config_.ipv6_gateway;
  const auto gateway_ipv6addr = gateway->ToIPv6Address();
  if (msg.mode() == net_base::RTNLMessage::kModeAdd) {
    network_config_.ipv6_gateway = gateway_ipv6addr;
    if (network_config_.ipv6_addresses.empty()) {
      StartNoPrefixTimer(kNoPrefixTimeOut);
    }
  } else if (msg.mode() == net_base::RTNLMessage::kModeDelete &&
             network_config_.ipv6_gateway == gateway_ipv6addr) {
    network_config_.ipv6_gateway = std::nullopt;
  }

  if (update_callback_ && old_gateway != network_config_.ipv6_gateway) {
    update_callback_.Run(UpdateType::kDefaultRoute);
  }
}

void SLAACController::NDOptionMsgHandler(const net_base::RTNLMessage& msg) {
  if (msg.interface_index() != interface_index_) {
    return;
  }

  switch (msg.type()) {
    case net_base::RTNLMessage::kTypeRdnss:
      RDNSSMsgHandler(msg);
      break;
    case net_base::RTNLMessage::kTypeDnssl:
      DNSSLMsgHandler(msg);
      break;
    case net_base::RTNLMessage::kTypeCaptivePortal:
      CaptivePortalMsgHandler(msg);
      break;
    case net_base::RTNLMessage::kTypePref64:
      Pref64MsgHandler(msg);
      break;
    case net_base::RTNLMessage::kTypePrefix:
      if (msg.prefix_status().prefix_flags & 0x10) {
        // draft-ietf-6man-pio-pflag: P-flag
        update_callback_.Run(UpdateType::kPFlag);
      }
      break;
    case net_base::RTNLMessage::kTypeNdUserOption:
      // Valid but unsupported ND option type.
      break;
    default:
      LOG(ERROR) << logging_tag_ << " " << __func__
                 << ": Unexpected RTNLMessage type " << msg.type();
  }
}

void SLAACController::RDNSSMsgHandler(const net_base::RTNLMessage& msg) {
  const net_base::RTNLMessage::RdnssOption& rdnss_option = msg.rdnss_option();
  uint32_t rdnss_lifetime_seconds = rdnss_option.lifetime;

  auto old_dns_servers = network_config_.dns_servers;
  network_config_.dns_servers.clear();
  for (const auto& rdnss : rdnss_option.addresses) {
    network_config_.dns_servers.push_back(net_base::IPAddress(rdnss));
  }

  // Stop any existing timer.
  StopRDNSSTimer();

  if (rdnss_lifetime_seconds == 0) {
    network_config_.dns_servers.clear();
  } else if (rdnss_lifetime_seconds != ND_OPT_LIFETIME_INFINITY) {
    // Setup timer to monitor DNS server lifetime if not infinite lifetime.
    base::TimeDelta delay = base::Seconds(rdnss_lifetime_seconds);
    StartRDNSSTimer(delay);
  }

  if (update_callback_ && old_dns_servers != network_config_.dns_servers) {
    update_callback_.Run(UpdateType::kRDNSS);
  }
}

void SLAACController::DNSSLMsgHandler(const net_base::RTNLMessage& msg) {
  const net_base::RTNLMessage::DnsslOption& dnssl_option = msg.dnssl_option();
  uint32_t dnssl_lifetime_seconds = dnssl_option.lifetime;

  auto old_domains = network_config_.dns_search_domains;
  network_config_.dns_search_domains = dnssl_option.domains;

  StopDNSSLTimer();

  if (dnssl_lifetime_seconds == 0) {
    network_config_.dns_search_domains.clear();
  } else if (dnssl_lifetime_seconds != ND_OPT_LIFETIME_INFINITY) {
    base::TimeDelta delay = base::Seconds(dnssl_lifetime_seconds);
    StartDNSSLTimer(delay);
  }

  if (update_callback_ && old_domains != network_config_.dns_search_domains) {
    update_callback_.Run(UpdateType::kDNSSL);
  }
}

void SLAACController::CaptivePortalMsgHandler(
    const net_base::RTNLMessage& msg) {
  const net_base::HttpUrl& uri = msg.captive_portal_uri();
  if (uri.protocol() != net_base::HttpUrl::Protocol::kHttps) {
    LOG(WARNING) << logging_tag_ << " " << __func__
                 << ": Captive portal URI should be HTTPS";
    return;
  }

  const bool is_updated = (network_config_.captive_portal_uri != uri);
  network_config_.captive_portal_uri = uri;
  if (update_callback_ && is_updated) {
    update_callback_.Run(UpdateType::kCaptivePortal);
  }
}

void SLAACController::Pref64MsgHandler(const net_base::RTNLMessage& msg) {
  // TODO(b/308893691): do not ignore lifetime of the pref64 option.
  if (msg.pref64().IsDefault()) {
    LOG(WARNING) << logging_tag_ << " " << __func__
                 << ": Ignoring invalid Pref64 info";
    return;
  }
  if (network_config_.pref64 == msg.pref64()) {
    return;
  }
  LOG(INFO) << logging_tag_ << " " << __func__ << ": " << msg.pref64();
  network_config_.pref64 = msg.pref64();
  if (update_callback_) {
    update_callback_.Run(UpdateType::kPref64);
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
  network_config_.dns_servers.clear();
  if (update_callback_) {
    update_callback_.Run(UpdateType::kRDNSS);
  }
}

void SLAACController::StartDNSSLTimer(base::TimeDelta delay) {
  dnssl_expired_callback_.Reset(base::BindOnce(&SLAACController::DNSSLExpired,
                                               weak_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, dnssl_expired_callback_.callback(),
                               delay);
}

void SLAACController::StopDNSSLTimer() {
  dnssl_expired_callback_.Cancel();
}

void SLAACController::DNSSLExpired() {
  network_config_.dns_search_domains.clear();
  if (update_callback_) {
    update_callback_.Run(UpdateType::kDNSSL);
  }
}

void SLAACController::StartNoPrefixTimer(base::TimeDelta delay) {
  no_prefix_callback_.Reset(base::BindOnce(
      &SLAACController::NoPrefixTimerExpired, weak_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, no_prefix_callback_.callback(),
                               delay);
}

void SLAACController::StopNoPrefixTimer() {
  no_prefix_callback_.Cancel();
}

void SLAACController::NoPrefixTimerExpired() {
  if (!network_config_.ipv6_addresses.empty()) {
    return;
  }
  if (update_callback_) {
    update_callback_.Run(UpdateType::kNoPrefix);
  }
}

net_base::NetworkConfig SLAACController::GetNetworkConfig() const {
  return network_config_;
}

void SLAACController::ConfigureLinkLocalAddress() {
  if (!link_local_address_) {
    return;
  }
  const auto link_local_mask =
      *net_base::IPv6CIDR::CreateFromStringAndPrefix("fe80::", 10);
  if (!link_local_mask.InSameSubnetWith(*link_local_address_)) {
    LOG(WARNING) << logging_tag_ << " " << __func__ << ": Address "
                 << *link_local_address_ << " is not a link local address";
    return;
  }
  LOG(INFO) << logging_tag_ << " " << __func__
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
    PLOG(WARNING) << logging_tag_ << " " << __func__
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
    PLOG(WARNING) << logging_tag_ << " " << __func__
                  << ": Error configuring hop limit in RS.";
  }

  if (sendto(sockfd, &packet, sizeof(packet), 0,
             reinterpret_cast<sockaddr*>(&dst_addr), sizeof(dst_addr)) < 0) {
    PLOG(WARNING) << logging_tag_ << " " << __func__ << ": Error sending RS.";
  }
}

std::optional<base::TimeDelta>
SLAACController::GetAndResetLastProvisionDuration() {
  if (!last_provision_timer_) {
    return std::nullopt;
  }

  if (last_provision_timer_->HasStarted()) {
    // The timer is still running, which means we haven't got any address.
    return std::nullopt;
  }

  base::TimeDelta ret;
  if (!last_provision_timer_->GetElapsedTime(&ret)) {
    // The timer has not been started. This shouldn't happen since Start() is
    // called right after the timer is created.
    return std::nullopt;
  }

  last_provision_timer_.reset();
  return ret;
}

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, SLAACController::UpdateType update_type) {
  switch (update_type) {
    case SLAACController::UpdateType::kAddress:
      return stream << "Address";
    case SLAACController::UpdateType::kRDNSS:
      return stream << "RDNSS";
    case SLAACController::UpdateType::kDefaultRoute:
      return stream << "DefaultRoute";
    case SLAACController::UpdateType::kDNSSL:
      return stream << "DNSSL";
    case SLAACController::UpdateType::kCaptivePortal:
      return stream << "CAPPORT";
    case SLAACController::UpdateType::kPFlag:
      return stream << "PFLAG";
    case SLAACController::UpdateType::kNoPrefix:
      return stream << "NoPrefix";
    case SLAACController::UpdateType::kPref64:
      return stream << "Pref64";
  }
}

}  // namespace shill
