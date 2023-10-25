// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/compound_network_config.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>

namespace shill {

CompoundNetworkConfig::CompoundNetworkConfig(std::string_view logging_tag)
    : logging_tag_(logging_tag) {
  combined_network_config_ = std::make_unique<NetworkConfig>();
}

CompoundNetworkConfig::~CompoundNetworkConfig() = default;

const NetworkConfig& CompoundNetworkConfig::Get() const {
  return *combined_network_config_;
}

const NetworkConfig* CompoundNetworkConfig::GetLegacySavedIPConfig() const {
  if (dhcp_network_config_) {
    return &*dhcp_network_config_;
  }
  if (link_protocol_network_config_) {
    return &*link_protocol_network_config_;
  }
  return nullptr;
}

bool CompoundNetworkConfig::HasSLAAC() {
  return slaac_network_config_ != nullptr;
}

void CompoundNetworkConfig::Clear() {
  link_protocol_network_config_ = nullptr;
  dhcp_network_config_ = nullptr;
  slaac_network_config_ = nullptr;
  static_network_config_ = {};
  Recalculate();
}

bool CompoundNetworkConfig::SetFromStatic(const NetworkConfig& config) {
  static_network_config_ = config;
  return Recalculate();
}

bool CompoundNetworkConfig::SetFromSLAAC(
    std::unique_ptr<NetworkConfig> config) {
  slaac_network_config_ = std::move(config);
  return Recalculate();
}

bool CompoundNetworkConfig::SetFromDHCP(std::unique_ptr<NetworkConfig> config) {
  dhcp_network_config_ = std::move(config);
  return Recalculate();
}

bool CompoundNetworkConfig::SetFromLinkProtocol(
    std::unique_ptr<NetworkConfig> config) {
  link_protocol_network_config_ = std::move(config);
  return Recalculate();
}

bool CompoundNetworkConfig::Recalculate() {
  auto old_network_config = std::move(combined_network_config_);
  combined_network_config_ = std::make_unique<NetworkConfig>();

  // We need to calculate the combined NetworkConfig item-by-item to support
  // existing usages such as IPv4 address from static + DNS from DHCP, or IPv4
  // address from DHCP + DNS from static, or IP/DNS from VPN + split routing
  // from static.

  // |ipv4_address|, |ipv4_broadcast|, and |ipv4_gateway| are always picked
  // from a same source. Preference order: static > DHCP > link. (DHCP and link
  // should not exists at once though.)
  if (link_protocol_network_config_ && dhcp_network_config_) {
    LOG(WARNING)
        << *this
        << ": both link local protocol config and DHCP are enabled. IPv4 "
           "address config from link local protocol will be ignored.";
  }
  bool has_static_ipv4_addr = static_network_config_.ipv4_address.has_value();
  bool has_dhcp_ipv4_addr =
      dhcp_network_config_ && dhcp_network_config_->ipv4_address;
  bool has_link_ipv4_addr = link_protocol_network_config_ &&
                            link_protocol_network_config_->ipv4_address;
  NetworkConfig empty_network_config;
  const NetworkConfig* preferred_ipv4_addr_src = &empty_network_config;
  if (has_static_ipv4_addr) {
    preferred_ipv4_addr_src = &static_network_config_;
  } else if (has_dhcp_ipv4_addr) {
    preferred_ipv4_addr_src = &*dhcp_network_config_;
  } else if (has_link_ipv4_addr) {
    preferred_ipv4_addr_src = &*link_protocol_network_config_;
  }
  combined_network_config_->ipv4_address =
      preferred_ipv4_addr_src->ipv4_address;
  combined_network_config_->ipv4_broadcast =
      preferred_ipv4_addr_src->ipv4_broadcast;
  combined_network_config_->ipv4_gateway =
      preferred_ipv4_addr_src->ipv4_gateway;

  // |ipv6_addresses|, and |ipv6_gateway| preference order: SLAAC > link.
  // SLAAC and link can co-exist on some cellular modems where SLAAC is turned
  // on for address but link is also needed for DNS.
  const NetworkConfig* preferred_ipv6_addr_src = &empty_network_config;
  if (slaac_network_config_) {
    preferred_ipv6_addr_src = &*slaac_network_config_;
  } else if (link_protocol_network_config_) {
    preferred_ipv6_addr_src = &*link_protocol_network_config_;
  }
  if (link_protocol_network_config_ && slaac_network_config_) {
    LOG(INFO)
        << *this
        << ": both link local protocol config and SLAAC are enabled. IPv6 "
           "address config from link local protocol will be ignored.";
  }
  combined_network_config_->ipv6_addresses =
      preferred_ipv6_addr_src->ipv6_addresses;
  combined_network_config_->ipv6_gateway =
      preferred_ipv6_addr_src->ipv6_gateway;

  // |ipv4_default_route| and |ipv6_blackhole_route| are only used for VPN.
  // Check |static_network_config_.ipv4_default_route| for the split-routing-VPN
  // through static-config use case. Other than this case it will always have
  // the default value (true).
  combined_network_config_->ipv4_default_route =
      static_network_config_.ipv4_default_route;
  if (link_protocol_network_config_) {
    combined_network_config_->ipv4_default_route &=
        link_protocol_network_config_->ipv4_default_route;
    combined_network_config_->ipv6_blackhole_route =
        link_protocol_network_config_->ipv6_blackhole_route;
  }

  // Excluded and included routing preference: static > link.
  // Usually only one of these two should have value though.
  if (!static_network_config_.excluded_route_prefixes.empty() ||
      !static_network_config_.included_route_prefixes.empty()) {
    combined_network_config_->excluded_route_prefixes =
        static_network_config_.excluded_route_prefixes;
    combined_network_config_->included_route_prefixes =
        static_network_config_.included_route_prefixes;
  } else if (link_protocol_network_config_) {
    combined_network_config_->excluded_route_prefixes =
        link_protocol_network_config_->excluded_route_prefixes;
    combined_network_config_->included_route_prefixes =
        link_protocol_network_config_->included_route_prefixes;
  }

  // |rfc3442_routes| can only be from DHCP.
  if (dhcp_network_config_) {
    combined_network_config_->rfc3442_routes =
        dhcp_network_config_->rfc3442_routes;
  }

  // DNS and DNSSL preference: static > non-static source merged.
  if (!static_network_config_.dns_servers.empty()) {
    combined_network_config_->dns_servers = static_network_config_.dns_servers;
  } else {
    for (const auto* properties :
         {slaac_network_config_.get(), link_protocol_network_config_.get(),
          dhcp_network_config_.get()}) {
      if (!properties) {
        continue;
      }
      combined_network_config_->dns_servers.insert(
          combined_network_config_->dns_servers.end(),
          properties->dns_servers.begin(), properties->dns_servers.end());
    }
  }
  if (!static_network_config_.dns_search_domains.empty()) {
    combined_network_config_->dns_search_domains =
        static_network_config_.dns_search_domains;
  } else {
    std::set<std::string> domain_search_dedup;
    for (const auto* properties :
         {slaac_network_config_.get(), link_protocol_network_config_.get(),
          dhcp_network_config_.get()}) {
      if (!properties) {
        continue;
      }
      for (const auto& item : properties->dns_search_domains) {
        if (domain_search_dedup.count(item) == 0) {
          combined_network_config_->dns_search_domains.push_back(item);
          domain_search_dedup.insert(item);
        }
      }
    }
  }

  // MTU preference: static > smallest value from DHCP, SLAAC and link.
  if (static_network_config_.mtu) {
    combined_network_config_->mtu = static_network_config_.mtu;
  } else {
    auto update_mtu = [this](const std::unique_ptr<NetworkConfig>& config) {
      if (config && config->mtu &&
          (!combined_network_config_->mtu ||
           combined_network_config_->mtu > config->mtu)) {
        combined_network_config_->mtu = config->mtu;
      }
    };
    update_mtu(dhcp_network_config_);
    update_mtu(slaac_network_config_);
    update_mtu(link_protocol_network_config_);
  }
  return *old_network_config != *combined_network_config_;
}

std::ostream& operator<<(std::ostream& stream,
                         const CompoundNetworkConfig& config) {
  stream << config.logging_tag_ << ":";
  if (!config.static_network_config_.IsEmpty()) {
    stream << " static " << config.static_network_config_ << ";";
  }
  if (config.link_protocol_network_config_) {
    stream << " data link layer " << *config.link_protocol_network_config_
           << ";";
  }
  if (config.dhcp_network_config_) {
    stream << " DHCP " << *config.dhcp_network_config_ << ";";
  }
  if (config.slaac_network_config_) {
    stream << " SLAAC " << *config.slaac_network_config_ << ";";
  }
  stream << " combined config " << *config.combined_network_config_;
  return stream;
}

}  // namespace shill
