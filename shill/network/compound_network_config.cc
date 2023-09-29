// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/compound_network_config.h"

#include <utility>

#include <base/logging.h>

namespace shill {

CompoundNetworkConfig::CompoundNetworkConfig(std::string_view logging_tag)
    : logging_tag_(logging_tag) {}

CompoundNetworkConfig::~CompoundNetworkConfig() = default;

const NetworkConfig& CompoundNetworkConfig::Get() const {
  return combined_network_config_;
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
  // TODO(b/269401899): currently the generated golden NetworkConfig contains
  // only IPv6 information. IPv4 in the process to be migrated.
  auto old_network_config = combined_network_config_;

  if (slaac_network_config_) {
    combined_network_config_ = *slaac_network_config_;
  }
  if (link_protocol_network_config_) {
    if (slaac_network_config_) {
      LOG(INFO) << logging_tag_
                << ": both link local protocol config and SLAAC are enabled. "
                   "Address config from link local protocol will be ignored.";
    } else {
      combined_network_config_.ipv6_addresses =
          link_protocol_network_config_->ipv6_addresses;
      combined_network_config_.ipv6_gateway =
          link_protocol_network_config_->ipv6_gateway;
    }
    combined_network_config_.dns_servers =
        link_protocol_network_config_->dns_servers;
    combined_network_config_.dns_search_domains =
        link_protocol_network_config_->dns_search_domains;
  }
  if (!static_network_config_.dns_search_domains.empty()) {
    combined_network_config_.dns_search_domains =
        static_network_config_.dns_search_domains;
  }
  if (!static_network_config_.dns_servers.empty()) {
    combined_network_config_.dns_servers = static_network_config_.dns_servers;
  }

  return old_network_config != combined_network_config_;
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
    stream << " DHCP " << *config.link_protocol_network_config_ << ";";
  }
  if (config.dhcp_network_config_) {
    stream << " SLAAC " << *config.link_protocol_network_config_ << ";";
  }
  stream << " combined config " << config.combined_network_config_;
  return stream;
}

}  // namespace shill
