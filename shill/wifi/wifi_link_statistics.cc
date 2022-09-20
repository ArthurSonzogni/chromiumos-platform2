// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_link_statistics.h"

#include <string>
#include <utility>
#include <vector>

#include <chromeos/dbus/service_constants.h>

#include "shill/logging.h"

namespace shill {
namespace {
std::string NetworkEventToString(WiFi::NetworkEvent event) {
  switch (event) {
    case WiFi::NetworkEvent::kUnknown:
      return "kUnknown";
    case WiFi::NetworkEvent::kIPConfigurationStart:
      return "kIPConfigurationStart";
    case WiFi::NetworkEvent::kConnected:
      return "kConnected";
    case WiFi::NetworkEvent::kDHCPRenewOnRoam:
      return "kDHCPRenewOnRoam";
    case WiFi::NetworkEvent::kDHCPSuccess:
      return "kDHCPSuccess";
    case WiFi::NetworkEvent::kDHCPFailure:
      return "kDHCPFailure";
    case WiFi::NetworkEvent::kSlaacFinished:
      return "kSlaacFinished";
    case WiFi::NetworkEvent::kNetworkValidationStart:
      return "kNetworkValidationStart";
    case WiFi::NetworkEvent::kNetworkValidationSuccess:
      return "kNetworkValidationSuccess";
    case WiFi::NetworkEvent::kNetworkValidationFailure:
      return "kNetworkValidationFailure";
    default:
      LOG(ERROR) << "Undefined NetworkEvent: " << (unsigned int)event;
      return "Undefined NetworkEvent";
  }
}

// Determine if the WiFi link statistics should be print to log.
bool ShouldPrintWiFiLinkStatistics(WiFi::NetworkEvent event) {
  // It doesn't consider if the service is connected (Service::IsConnected() ==
  // true) when determining if the WiFi link statistics should be printed.
  // There are two examples where the service is connected, but the necessity of
  // WiFi link statistics differs.

  // 1. For IPv6-only networks, the network event transition may be
  // kIPConfigurationStart -> kSlaacFinished -> kConnected -> kDHCPFailure, the
  // WiFi link statistics should not be printed.
  // 2. Suppose a device has a static IPv4 configuration but it still needs DHCP
  // to succeed (to obtain vendor options, like proxy settings) and DHCP fails
  // due to poor link connection, the WiFi link statistics should be printed.

  // It may print unnecessary WiFi link statistics if the state of the service
  // is not considered. It is acceptable because the size of the WiFi link
  // statistics in netlog is small.
  return event == WiFi::NetworkEvent::kDHCPFailure ||
         event == WiFi::NetworkEvent::kNetworkValidationFailure;
}

// Calculate the difference between NL80211 link statistics old_stats and
// new_stats
WiFiLinkStatistics::Nl80211LinkStatistics Nl80211LinkStatisticsDiff(
    const WiFiLinkStatistics::Nl80211LinkStatistics& old_stats,
    const WiFiLinkStatistics::Nl80211LinkStatistics& new_stats) {
  WiFiLinkStatistics::Nl80211LinkStatistics diff_stats;
  diff_stats.rx_packets_success =
      new_stats.rx_packets_success - old_stats.rx_packets_success;
  diff_stats.tx_packets_success =
      new_stats.tx_packets_success - old_stats.tx_packets_success;
  diff_stats.rx_bytes_success =
      new_stats.rx_bytes_success - old_stats.rx_bytes_success;
  diff_stats.tx_bytes_success =
      new_stats.tx_bytes_success - old_stats.tx_bytes_success;
  diff_stats.tx_packets_failure =
      new_stats.tx_packets_failure - old_stats.tx_packets_failure;
  diff_stats.tx_retries = new_stats.tx_retries - old_stats.tx_retries;
  diff_stats.rx_packets_dropped =
      new_stats.rx_packets_dropped - old_stats.rx_packets_dropped;
  diff_stats.last_rx_signal_dbm = new_stats.last_rx_signal_dbm;
  diff_stats.avg_rx_signal_dbm = new_stats.avg_rx_signal_dbm;
  return diff_stats;
}

// Calculate the difference between RTNL link statistics old_stats and
// new_stats
old_rtnl_link_stats64 RtnlLinkStatisticsDiff(
    const old_rtnl_link_stats64& old_stats,
    const old_rtnl_link_stats64& new_stats) {
  old_rtnl_link_stats64 diff_stats;
  diff_stats.rx_packets = new_stats.rx_packets - old_stats.rx_packets;
  diff_stats.tx_packets = new_stats.tx_packets - old_stats.tx_packets;
  diff_stats.rx_bytes = new_stats.rx_bytes - old_stats.rx_bytes;
  diff_stats.tx_bytes = new_stats.tx_bytes - old_stats.tx_bytes;
  diff_stats.rx_errors = new_stats.rx_errors - old_stats.rx_errors;
  diff_stats.tx_errors = new_stats.tx_errors - old_stats.tx_errors;
  diff_stats.rx_dropped = new_stats.rx_dropped - old_stats.rx_dropped;
  diff_stats.tx_dropped = new_stats.tx_dropped - old_stats.tx_dropped;
  return diff_stats;
}

// Convert RTNL link statistics to string
std::string RtnlLinkStatisticsToString(
    const old_rtnl_link_stats64& diff_stats) {
  return "rx_packets " + std::to_string(diff_stats.rx_packets) +
         " tx_packets " + std::to_string(diff_stats.tx_packets) + " rx_bytes " +
         std::to_string(diff_stats.rx_bytes) + " tx_bytes " +
         std::to_string(diff_stats.tx_bytes) + " rx_errors " +
         std::to_string(diff_stats.rx_errors) + " tx_errors " +
         std::to_string(diff_stats.tx_errors) + " rx_dropped " +
         std::to_string(diff_stats.rx_dropped) + " tx_dropped " +
         std::to_string(diff_stats.tx_dropped);
}

// Convert NL80211 link statistics to string
std::string Nl80211LinkStatisticsToString(
    const WiFiLinkStatistics::Nl80211LinkStatistics& diff_stats) {
  return std::string(kPacketReceiveSuccessesProperty) + " " +
         std::to_string(diff_stats.rx_packets_success) + " " +
         kPacketTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx_packets_success) + " " +
         kByteReceiveSuccessesProperty + " " +
         std::to_string(diff_stats.rx_bytes_success) + " " +
         kByteTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx_bytes_success) + " " +
         kPacketTransmitFailuresProperty + " " +
         std::to_string(diff_stats.tx_packets_failure) + " " +
         kTransmitRetriesProperty + " " +
         std::to_string(diff_stats.tx_retries) + " " +
         kPacketReceiveDropProperty + " " +
         std::to_string(diff_stats.rx_packets_dropped) +
         "; the current signal information: " + kLastReceiveSignalDbmProperty +
         " " + std::to_string(diff_stats.last_rx_signal_dbm) + " " +
         kAverageReceiveSignalDbmProperty + " " +
         std::to_string(diff_stats.avg_rx_signal_dbm);
}
}  // namespace

void WiFiLinkStatistics::UpdateNl80211LinkStatistics(
    WiFi::NetworkEvent current_network_event,
    const KeyValueStore& link_statistics) {
  // nl80211 station information for WiFi link diagnosis
  if (current_network_event == WiFi::NetworkEvent::kUnknown) {
    return;
  }
  Nl80211LinkStatistics stats;
  std::vector<std::pair<std::string, uint32_t*>>
      nl80211_sta_info_properties_u32 = {
          {kPacketReceiveSuccessesProperty, &stats.rx_packets_success},
          {kPacketTransmitSuccessesProperty, &stats.tx_packets_success},
          {kByteReceiveSuccessesProperty, &stats.rx_bytes_success},
          {kByteTransmitSuccessesProperty, &stats.tx_bytes_success},
          {kPacketTransmitFailuresProperty, &stats.tx_packets_failure},
          {kTransmitRetriesProperty, &stats.tx_retries}};

  for (const auto& kv : nl80211_sta_info_properties_u32) {
    if (link_statistics.Contains<uint32_t>(kv.first)) {
      *kv.second = link_statistics.Get<uint32_t>(kv.first);
    }
  }

  std::vector<std::pair<std::string, uint64_t*>>
      nl80211_sta_info_properties_u64 = {
          {kPacketReceiveDropProperty, &stats.rx_packets_dropped}};

  for (const auto& kv : nl80211_sta_info_properties_u64) {
    if (link_statistics.Contains<uint64_t>(kv.first)) {
      *kv.second = link_statistics.Get<uint64_t>(kv.first);
    }
  }

  std::vector<std::pair<std::string, int32_t*>>
      nl80211_sta_info_properties_s32 = {
          {kLastReceiveSignalDbmProperty, &stats.last_rx_signal_dbm},
          {kAverageReceiveSignalDbmProperty, &stats.avg_rx_signal_dbm}};

  for (const auto& kv : nl80211_sta_info_properties_s32) {
    if (link_statistics.Contains<int32_t>(kv.first)) {
      *kv.second = link_statistics.Get<int32_t>(kv.first);
    }
  }

  const auto current_time = base::Time::Now();
  if (ShouldPrintWiFiLinkStatistics(current_network_event)) {
    Nl80211LinkStatistics diff_stats =
        Nl80211LinkStatisticsDiff(nl80211_link_statistics_, stats);
    LOG(INFO) << "Network event related to NL80211 link statistics: "
              << NetworkEventToString(nl80211_network_event_) << " -> "
              << NetworkEventToString(current_network_event)
              << "; the NL80211 link statistics delta for the last "
              << std::to_string((current_time - nl80211_timestamp_).InSeconds())
              << " seconds is " << Nl80211LinkStatisticsToString(diff_stats);
  }
  nl80211_link_statistics_ = stats;
  nl80211_network_event_ = current_network_event;
  nl80211_timestamp_ = current_time;
}

void WiFiLinkStatistics::UpdateRtnlLinkStatistics(
    WiFi::NetworkEvent current_network_event,
    const old_rtnl_link_stats64& stats) {
  if (current_network_event == WiFi::NetworkEvent::kUnknown) {
    return;
  }
  const auto current_time = base::Time::Now();
  if (ShouldPrintWiFiLinkStatistics(current_network_event)) {
    old_rtnl_link_stats64 diff_stats =
        RtnlLinkStatisticsDiff(rtnl_link_statistics_, stats);
    LOG(INFO) << "Network event related to RTNL link statistics: "
              << NetworkEventToString(rtnl_network_event_) << " -> "
              << NetworkEventToString(current_network_event)
              << "; the RTNL link statistics delta for the last "
              << std::to_string((current_time - rtnl_timestamp_).InSeconds())
              << " seconds is " << RtnlLinkStatisticsToString(diff_stats);
  }
  rtnl_link_statistics_ = stats;
  rtnl_network_event_ = current_network_event;
  rtnl_timestamp_ = current_time;
}

}  // namespace shill
