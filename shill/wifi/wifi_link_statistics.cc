// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_link_statistics.h"

#include <string>
#include <utility>
#include <vector>
#include "base/strings/stringprintf.h"
#include "shill/store/key_value_store.h"

#include <chromeos/dbus/service_constants.h>

namespace shill {
namespace {
// Determine if the WiFi link statistics should be print to log.
bool ShouldPrintWiFiLinkStatistics(WiFiLinkStatistics::Trigger trigger) {
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
  return trigger == WiFiLinkStatistics::Trigger::kDHCPFailure ||
         trigger == WiFiLinkStatistics::Trigger::kNetworkValidationFailure;
}

bool IsEndNetworkEvent(WiFiLinkStatistics::Trigger trigger) {
  return trigger == WiFiLinkStatistics::Trigger::kConnected ||
         trigger == WiFiLinkStatistics::Trigger::kDHCPSuccess ||
         trigger == WiFiLinkStatistics::Trigger::kDHCPFailure ||
         trigger == WiFiLinkStatistics::Trigger::kSlaacFinished ||
         trigger == WiFiLinkStatistics::Trigger::kNetworkValidationSuccess ||
         trigger == WiFiLinkStatistics::Trigger::kNetworkValidationFailure;
}

bool DoesEndMatchStartEvent(WiFiLinkStatistics::Trigger start_event,
                            WiFiLinkStatistics::Trigger end_event) {
  // kIPConfigurationStart is used to represent IPv4 and IPv6 configuration
  // start, so kConnected doesn't actually have a corresponding start event.
  switch (end_event) {
    case WiFiLinkStatistics::Trigger::kDHCPSuccess:
    case WiFiLinkStatistics::Trigger::kDHCPFailure:
      return start_event ==
                 WiFiLinkStatistics::Trigger::kIPConfigurationStart ||
             start_event == WiFiLinkStatistics::Trigger::kDHCPRenewOnRoam;
    case WiFiLinkStatistics::Trigger::kSlaacFinished:
      return start_event == WiFiLinkStatistics::Trigger::kIPConfigurationStart;
    case WiFiLinkStatistics::Trigger::kNetworkValidationSuccess:
    case WiFiLinkStatistics::Trigger::kNetworkValidationFailure:
      return start_event ==
             WiFiLinkStatistics::Trigger::kNetworkValidationStart;
    default:
      return false;
  }
}

// Calculate the difference between NL80211 link statistics old_stats and
// new_stats
WiFiLinkStatistics::StationStats Nl80211LinkStatisticsDiff(
    const WiFiLinkStatistics::StationStats& old_stats,
    const WiFiLinkStatistics::StationStats& new_stats) {
  WiFiLinkStatistics::StationStats diff_stats;
  diff_stats.rx.packets = new_stats.rx.packets - old_stats.rx.packets;
  diff_stats.tx.packets = new_stats.tx.packets - old_stats.tx.packets;
  diff_stats.rx.bytes = new_stats.rx.bytes - old_stats.rx.bytes;
  diff_stats.tx.bytes = new_stats.tx.bytes - old_stats.tx.bytes;
  diff_stats.tx_failed = new_stats.tx_failed - old_stats.tx_failed;
  diff_stats.tx_retries = new_stats.tx_retries - old_stats.tx_retries;
  diff_stats.rx_drop_misc = new_stats.rx_drop_misc - old_stats.rx_drop_misc;
  diff_stats.signal = new_stats.signal;
  diff_stats.signal_avg = new_stats.signal_avg;
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
    const WiFiLinkStatistics::StationStats& diff_stats) {
  return std::string(kPacketReceiveSuccessesProperty) + " " +
         std::to_string(diff_stats.rx.packets) + " " +
         kPacketTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx.packets) + " " +
         kByteReceiveSuccessesProperty + " " +
         std::to_string(diff_stats.rx.bytes) + " " +
         kByteTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx.bytes) + " " +
         kPacketTransmitFailuresProperty + " " +
         std::to_string(diff_stats.tx_failed) + " " + kTransmitRetriesProperty +
         " " + std::to_string(diff_stats.tx_retries) + " " +
         kPacketReceiveDropProperty + " " +
         std::to_string(diff_stats.rx_drop_misc) +
         "; the current signal information: " + kLastReceiveSignalDbmProperty +
         " " + std::to_string(diff_stats.signal) + " " +
         kAverageReceiveSignalDbmProperty + " " +
         std::to_string(diff_stats.signal_avg);
}

std::string ConvertToBitrateString(WiFiLinkStatistics::LinkStats link_stats) {
  std::string mcs_str;
  switch (link_stats.mode) {
    case WiFiLinkStatistics::LinkMode::kLinkModeLegacy:
      mcs_str = base::StringPrintf(" MCS %d", link_stats.mcs);
      break;
    case WiFiLinkStatistics::LinkMode::kLinkModeVHT:
      mcs_str = base::StringPrintf(" VHT-MCS %d", link_stats.mcs);
      break;
    default:
      break;
  }

  std::string nss_str;
  WiFiLinkStatistics::LinkStats defaults;
  if (link_stats.nss != defaults.nss) {
    nss_str = base::StringPrintf(" VHT-NSS %d", link_stats.nss);
  }

  std::string width_str;
  switch (link_stats.width) {
    case WiFiLinkStatistics::ChannelWidth::kChannelWidth40MHz:
      width_str = base::StringPrintf(" 40MHz");
      break;
    case WiFiLinkStatistics::ChannelWidth::kChannelWidth80MHz:
      width_str = base::StringPrintf(" 80MHz");
      break;
    case WiFiLinkStatistics::ChannelWidth::kChannelWidth80p80MHz:
      width_str = base::StringPrintf(" 80+80MHz");
      break;
    case WiFiLinkStatistics::ChannelWidth::kChannelWidth160MHz:
      width_str = base::StringPrintf(" 160MHz");
      break;
    default:
      break;
  }

  std::string out = base::StringPrintf(
      "%d.%d MBit/s%s%s%s%s", link_stats.bitrate / 10, link_stats.bitrate % 10,
      mcs_str.c_str(), width_str.c_str(),
      link_stats.gi == WiFiLinkStatistics::GuardInterval::kLinkStatsGI_0_4
          ? " short GI"
          : "",
      nss_str.c_str());
  return out;
}

}  // namespace

// static
std::string WiFiLinkStatistics::LinkStatisticsTriggerToString(Trigger trigger) {
  switch (trigger) {
    case Trigger::kUnknown:
      return "kUnknown";
    case Trigger::kIPConfigurationStart:
      return "kIPConfigurationStart";
    case Trigger::kConnected:
      return "kConnected";
    case Trigger::kDHCPRenewOnRoam:
      return "kDHCPRenewOnRoam";
    case Trigger::kDHCPSuccess:
      return "kDHCPSuccess";
    case Trigger::kDHCPFailure:
      return "kDHCPFailure";
    case Trigger::kSlaacFinished:
      return "kSlaacFinished";
    case Trigger::kNetworkValidationStart:
      return "kNetworkValidationStart";
    case Trigger::kNetworkValidationSuccess:
      return "kNetworkValidationSuccess";
    case Trigger::kNetworkValidationFailure:
      return "kNetworkValidationFailure";
    default:
      LOG(ERROR) << "Invalid LinkStatisticsTrigger: "
                 << static_cast<unsigned int>(trigger);
      return "Invalid";
  }
}

// static
KeyValueStore WiFiLinkStatistics::StationStatsToKV(const StationStats& stats) {
  KeyValueStore kv;
  StationStats defaults;
  if (stats.inactive_time != defaults.inactive_time) {
    kv.Set<uint32_t>(kInactiveTimeMillisecondsProperty, stats.inactive_time);
  }
  if (stats.rx.packets != defaults.rx.packets) {
    kv.Set<uint32_t>(kPacketReceiveSuccessesProperty, stats.rx.packets);
  }
  if (stats.tx.packets != defaults.tx.packets) {
    kv.Set<uint32_t>(kPacketTransmitSuccessesProperty, stats.tx.packets);
  }
  if (stats.rx.bytes != defaults.rx.bytes) {
    kv.Set<uint32_t>(kByteReceiveSuccessesProperty, stats.rx.bytes);
  }
  if (stats.tx.bytes != defaults.tx.bytes) {
    kv.Set<uint32_t>(kByteTransmitSuccessesProperty, stats.tx.bytes);
  }
  if (stats.tx_failed != defaults.tx_failed) {
    kv.Set<uint32_t>(kPacketTransmitFailuresProperty, stats.tx_failed);
  }
  if (stats.tx_retries != defaults.tx_retries) {
    kv.Set<uint32_t>(kTransmitRetriesProperty, stats.tx_retries);
  }
  if (stats.rx_drop_misc != defaults.rx_drop_misc) {
    kv.Set<uint64_t>(kPacketReceiveDropProperty, stats.rx_drop_misc);
  }

  if (stats.signal != defaults.signal) {
    kv.Set<int32_t>(kLastReceiveSignalDbmProperty, stats.signal);
  }
  if (stats.signal_avg != defaults.signal_avg) {
    kv.Set<int32_t>(kAverageReceiveSignalDbmProperty, stats.signal_avg);
  }

  if (stats.tx.bitrate != defaults.tx.bitrate) {
    kv.Set<std::string>(kTransmitBitrateProperty,
                        ConvertToBitrateString(stats.tx));
  }
  if (stats.rx.bitrate != defaults.rx.bitrate) {
    kv.Set<std::string>(kReceiveBitrateProperty,
                        ConvertToBitrateString(stats.rx));
  }
  return kv;
}

void WiFiLinkStatistics::Reset() {
  nl80211_link_statistics_.clear();
  rtnl_link_statistics_.clear();
}

void WiFiLinkStatistics::UpdateNl80211LinkStatistics(
    Trigger trigger, const StationStats& stats) {
  // nl80211 station information for WiFi link diagnosis
  if (trigger == Trigger::kUnknown) {
    return;
  }

  // If the trigger is an end network event, erase the link statistics of its
  // start network event and print the difference to the log if necessary.
  if (IsEndNetworkEvent(trigger)) {
    for (auto it = nl80211_link_statistics_.begin();
         it != nl80211_link_statistics_.end(); it++) {
      if (!DoesEndMatchStartEvent(it->trigger, trigger)) {
        continue;
      }
      if (ShouldPrintWiFiLinkStatistics(trigger)) {
        auto diff_stats =
            Nl80211LinkStatisticsDiff(it->nl80211_link_stats, stats);
        LOG(INFO) << "Network event related to NL80211 link statistics: "
                  << LinkStatisticsTriggerToString(it->trigger) << " -> "
                  << LinkStatisticsTriggerToString(trigger)
                  << "; the NL80211 link statistics delta for the last "
                  << std::to_string(
                         (base::Time::Now() - it->timestamp).InSeconds())
                  << " seconds is "
                  << Nl80211LinkStatisticsToString(diff_stats);
      }
      nl80211_link_statistics_.erase(it);
      break;
    }
  } else {
    // The trigger is a start network event, append this snapshot of link
    // statistics.
    nl80211_link_statistics_.emplace_back(trigger, stats);
    // Add an extra nl80211 link statistics because kIPConfigurationStart
    // corresponds to the start of the initial DHCP lease acquisition by dhcpcd
    // and to the start of IPv6 SLAAC in the kernel.
    if (trigger == Trigger::kIPConfigurationStart) {
      nl80211_link_statistics_.emplace_back(trigger, stats);
    }
  }
}

void WiFiLinkStatistics::UpdateRtnlLinkStatistics(
    Trigger trigger, const old_rtnl_link_stats64& stats) {
  if (trigger == Trigger::kUnknown) {
    return;
  }
  // If the trigger is an end network event, erase the link statistics of its
  // start network event and print the difference to the log if necessary.
  if (IsEndNetworkEvent(trigger)) {
    for (auto it = rtnl_link_statistics_.begin();
         it != rtnl_link_statistics_.end(); it++) {
      if (!DoesEndMatchStartEvent(it->trigger, trigger)) {
        continue;
      }
      if (ShouldPrintWiFiLinkStatistics(trigger)) {
        auto diff_stats = RtnlLinkStatisticsDiff(it->rtnl_link_stats, stats);
        LOG(INFO) << "Network event related to RTNL link statistics: "
                  << LinkStatisticsTriggerToString(it->trigger) << " -> "
                  << LinkStatisticsTriggerToString(trigger)
                  << "; the RTNL link statistics delta for the last "
                  << std::to_string(
                         (base::Time::Now() - it->timestamp).InSeconds())
                  << " seconds is " << RtnlLinkStatisticsToString(diff_stats);
      }
      rtnl_link_statistics_.erase(it);
      break;
    }
  } else {
    // The trigger is a start network event, append this snapshot of link
    // statistics.
    rtnl_link_statistics_.emplace_back(trigger, stats);
    // Add an extra RTNL link statistics because kIPConfigurationStart
    // corresponds to the start of the initial DHCP lease acquisition by dhcpcd
    // and to the start of IPv6 SLAAC in the kernel.
    if (trigger == Trigger::kIPConfigurationStart) {
      rtnl_link_statistics_.emplace_back(trigger, stats);
    }
  }
}

}  // namespace shill
