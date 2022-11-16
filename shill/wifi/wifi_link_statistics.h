// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_LINK_STATISTICS_H_
#define SHILL_WIFI_WIFI_LINK_STATISTICS_H_

#include <cstdint>
#include <list>
#include <string>

#include <base/time/time.h>

#include "shill/mockable.h"
#include "shill/net/rtnl_link_stats.h"
#include "shill/store/key_value_store.h"
#include "shill/wifi/wifi.h"

namespace shill {

class WiFiLinkStatistics {
 public:
  enum class ChannelWidth {
    kChannelWidthUnknown,
    kChannelWidth20MHz,
    kChannelWidth40MHz,
    kChannelWidth80MHz,
    kChannelWidth80p80MHz,  // 80+80MHz channel configuration.
    kChannelWidth160MHz,
    kChannelWidth320MHz,
  };
  enum class LinkMode {
    kLinkModeUnknown,
    kLinkModeLegacy,
    kLinkModeVHT,
    kLinkModeHE,
    kLinkModeEHT,
  };

  enum class GuardInterval {
    kLinkStatsGIUnknown,
    kLinkStatsGI_0_4,
    kLinkStatsGI_0_8,
    kLinkStatsGI_1_6,
    kLinkStatsGI_3_2,
  };

  struct LinkStats {
    uint32_t packets = -1;
    uint32_t bytes = -1;
    uint32_t bitrate = -1;  // unit is 100Kb/s.
    uint8_t mcs = -1;
    ChannelWidth width = ChannelWidth::kChannelWidthUnknown;
    LinkMode mode = LinkMode::kLinkModeUnknown;
    GuardInterval gi = GuardInterval::kLinkStatsGIUnknown;
    uint8_t nss = -1;
    uint8_t dcm = -1;
  };

  struct StationStats {
    uint32_t inactive_time = -1;
    uint32_t tx_retries = -1;
    uint32_t tx_failed = -1;
    uint64_t rx_drop_misc = -1;
    int32_t signal = 9999;  // wpa_supplicant uses int32_t value, default 9999.
    int32_t signal_avg = 9999;
    LinkStats rx;
    LinkStats tx;
  };

  struct Nl80211LinkStatistics {
    // The event that triggered the snapshot of WiFiLinkStatistics.
    WiFi::LinkStatisticsTrigger trigger = WiFi::LinkStatisticsTrigger::kUnknown;
    base::Time timestamp;
    StationStats nl80211_link_stats;
    Nl80211LinkStatistics(WiFi::LinkStatisticsTrigger trigger,
                          const StationStats& stats)
        : trigger(trigger), nl80211_link_stats(stats) {
      timestamp = base::Time::Now();
    }
  };

  struct RtnlLinkStatistics {
    // The event that triggered the snapshot of WiFiLinkStatistics.
    WiFi::LinkStatisticsTrigger trigger = WiFi::LinkStatisticsTrigger::kUnknown;
    base::Time timestamp;
    old_rtnl_link_stats64 rtnl_link_stats;
    RtnlLinkStatistics(WiFi::LinkStatisticsTrigger event,
                       const old_rtnl_link_stats64& stats)
        : trigger(event), rtnl_link_stats(stats) {
      timestamp = base::Time::Now();
    }
  };

  WiFiLinkStatistics() = default;
  virtual ~WiFiLinkStatistics() = default;

  // Clear all existing nl80211 and RTNL link statistics in the lists
  void Reset();

  static std::string LinkStatisticsTriggerToString(
      WiFi::LinkStatisticsTrigger event);

  // Update a new snapshot of WiFi link statistics.
  // If trigger is a start network event, the WiFi link statistics is
  // appended to the WiFi link statistics list; if it is an end network
  // event, pop the WiFi link statistics of the corresponding start network
  // event from the list and print the difference if the end network event is a
  // failure.

  // Each network activity must call WiFi::RetrieveLinkStatistics() at both
  // start network event and end network event; otherwise, the WiFi link
  // statistics of the start network event is left in the list and matches
  // the wrong end network event.
  mockable void UpdateNl80211LinkStatistics(
      WiFi::LinkStatisticsTrigger trigger,
      const KeyValueStore& link_statistics);
  mockable void UpdateRtnlLinkStatistics(WiFi::LinkStatisticsTrigger trigger,
                                         const old_rtnl_link_stats64& stats);

 private:
  friend class WiFiLinkStatisticsTest;

  // The snapshot of link statistics is updated if the trigger is not
  // kUnknown. The difference between the end and start network events is
  // printed to the log if necessary, i.e., the end network event is a failure,
  // such as kDHCPFailure, kNetworkValidationFailure
  std::list<Nl80211LinkStatistics> nl80211_link_statistics_;
  std::list<RtnlLinkStatistics> rtnl_link_statistics_;
};

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_LINK_STATISTICS_H_
