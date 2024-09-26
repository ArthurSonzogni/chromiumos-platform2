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
  combined_network_config_ = std::make_unique<net_base::NetworkConfig>();
}

CompoundNetworkConfig::~CompoundNetworkConfig() = default;

const net_base::NetworkConfig& CompoundNetworkConfig::Get() const {
  return *combined_network_config_;
}

const net_base::NetworkConfig* CompoundNetworkConfig::GetLegacySavedIPConfig()
    const {
  if (dhcp_network_config_) {
    return &*dhcp_network_config_;
  }
  if (link_protocol_network_config_) {
    return &*link_protocol_network_config_;
  }
  return nullptr;
}

bool CompoundNetworkConfig::HasSLAAC() {
  return combined_network_config_->ipv6_addresses ==
         slaac_network_config_->ipv6_addresses;
}

void CompoundNetworkConfig::ClearNonStaticConfigs() {
  link_protocol_network_config_ = nullptr;
  dhcp_network_config_ = nullptr;
  slaac_network_config_ = nullptr;
  Recalculate();
}

bool CompoundNetworkConfig::SetFromStatic(
    const net_base::NetworkConfig& config) {
  static_network_config_ = config;
  return Recalculate();
}

bool CompoundNetworkConfig::SetFromSLAAC(
    std::unique_ptr<net_base::NetworkConfig> config) {
  slaac_network_config_ = std::move(config);
  return Recalculate();
}

bool CompoundNetworkConfig::SetFromDHCP(
    std::unique_ptr<net_base::NetworkConfig> config) {
  dhcp_network_config_ = std::move(config);
  return Recalculate();
}

bool CompoundNetworkConfig::SetFromDHCPv6(
    std::unique_ptr<net_base::NetworkConfig> config) {
  dhcpv6_network_config_ = std::move(config);
  return Recalculate();
}

bool CompoundNetworkConfig::SetFromLinkProtocol(
    std::unique_ptr<net_base::NetworkConfig> config) {
  link_protocol_network_config_ = std::move(config);
  return Recalculate();
}

bool CompoundNetworkConfig::Recalculate() {
  auto old_network_config = std::move(combined_network_config_);
  combined_network_config_ = std::make_unique<net_base::NetworkConfig>();

  // We need to calculate the combined net_base::NetworkConfig item-by-item to
  // support existing usages such as IPv4 address from static + DNS from DHCP,
  // or IPv4 address from DHCP + DNS from static, or IP/DNS from VPN + split
  // routing from static.

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
  net_base::NetworkConfig empty_network_config;
  const net_base::NetworkConfig* preferred_ipv4_addr_src =
      &empty_network_config;
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
  const net_base::NetworkConfig* preferred_ipv6_addr_src =
      &empty_network_config;
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

  // DHCPv6 has the highest preference order for |ipv6_addresses|. It still
  // relies on SLAAC for |ipv6_gateway|.
  if (dhcpv6_network_config_) {
    combined_network_config_->ipv6_addresses =
        dhcpv6_network_config_->ipv6_addresses;
    combined_network_config_->ipv6_delegated_prefixes =
        dhcpv6_network_config_->ipv6_delegated_prefixes;
  }

  // |ipv6_blackhole_route| is only used for VPN.
  if (link_protocol_network_config_) {
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

  // |captive_portal_uri| can be from DHCP or SLAAC. Use the value coming first.
  if (!(dhcp_network_config_ &&
        dhcp_network_config_->captive_portal_uri.has_value()) &&
      !(slaac_network_config_ &&
        slaac_network_config_->captive_portal_uri.has_value())) {
    // No captive portal URI found, do nothing.
  } else if (old_network_config->captive_portal_uri.has_value()) {
    combined_network_config_->captive_portal_uri =
        old_network_config->captive_portal_uri;
  } else if (dhcp_network_config_ &&
             dhcp_network_config_->captive_portal_uri.has_value()) {
    combined_network_config_->captive_portal_uri =
        dhcp_network_config_->captive_portal_uri;
  } else if (slaac_network_config_ &&
             slaac_network_config_->captive_portal_uri.has_value()) {
    combined_network_config_->captive_portal_uri =
        slaac_network_config_->captive_portal_uri;
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
  // Remove empty DNS addresses since they are not meaningful. StaticIPConfig
  // generated from UI may contain them.
  std::erase_if(
      combined_network_config_->dns_servers,
      [](const net_base::IPAddress& ip) -> bool { return ip.IsZero(); });
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
  if (static_network_config_.mtu && static_network_config_.mtu > 0) {
    combined_network_config_->mtu = static_network_config_.mtu;
  } else {
    auto update_mtu =
        [this](const std::unique_ptr<net_base::NetworkConfig>& config) {
          if (config && config->mtu && config->mtu > 0 &&
              (!combined_network_config_->mtu ||
               combined_network_config_->mtu > config->mtu)) {
            combined_network_config_->mtu = config->mtu;
          }
        };
    update_mtu(dhcp_network_config_);
    update_mtu(dhcpv6_network_config_);
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
  if (config.dhcpv6_network_config_) {
    stream << " DHCPv6 " << *config.dhcp_network_config_ << ";";
  }
  stream << " combined config " << *config.combined_network_config_;
  return stream;
}

}  // namespace shill
