// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_LINK_STATISTICS_H_
#define SHILL_WIFI_WIFI_LINK_STATISTICS_H_

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
  struct Nl80211StaInfo {
    uint32_t rx_packets_success;
    uint32_t tx_packets_success;
    uint32_t rx_bytes_success;
    uint32_t tx_bytes_success;
    uint32_t tx_packets_failure;
    uint32_t tx_retries;
    uint64_t rx_packets_dropped;
    int32_t last_rx_signal_dbm;
    int32_t avg_rx_signal_dbm;
  };

  struct Nl80211LinkStatistics {
    // The event that triggered the snapshot of WiFiLinkStatistics.
    WiFi::LinkStatisticsTrigger trigger = WiFi::LinkStatisticsTrigger::kUnknown;
    base::Time timestamp;
    Nl80211StaInfo nl80211_link_stats;
    Nl80211LinkStatistics(WiFi::LinkStatisticsTrigger trigger,
                          const Nl80211StaInfo& stats)
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
