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

struct nl80211_sta_info {
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

class WiFiLinkStatistics {
 public:
  struct Nl80211LinkStatistics {
    // The NetworkEvent at which the snapshot of WiFiLinkStatistics was made
    WiFi::NetworkEvent network_event = WiFi::NetworkEvent::kUnknown;
    base::Time timestamp;
    nl80211_sta_info nl80211_link_stats;
    Nl80211LinkStatistics(WiFi::NetworkEvent event,
                          const nl80211_sta_info& stats)
        : network_event(event), nl80211_link_stats(stats) {
      timestamp = base::Time::Now();
    }
  };

  struct RtnlLinkStatistics {
    // The NetworkEvent at which the snapshot of WiFiLinkStatistics was made
    WiFi::NetworkEvent network_event = WiFi::NetworkEvent::kUnknown;
    base::Time timestamp;
    old_rtnl_link_stats64 rtnl_link_stats;
    RtnlLinkStatistics(WiFi::NetworkEvent event,
                       const old_rtnl_link_stats64& stats)
        : network_event(event), rtnl_link_stats(stats) {
      timestamp = base::Time::Now();
    }
  };

  WiFiLinkStatistics() = default;
  virtual ~WiFiLinkStatistics() = default;

  // Clear all existing nl80211 and RTNL link statistics in the lists
  void Reset();

  static std::string NetworkEventToString(WiFi::NetworkEvent event);

  // Update a new snapshot of WiFi link statistics.
  // If network_event is a start network event, the WiFi link statistics is
  // appended to the WiFi link statistics list; if it is an end network
  // event, pop the WiFi link statistics of the corresponding start network
  // event from the list and print the difference if the end network event is a
  // failure.

  // Each network activity must call WiFi::RetrieveLinkStatistics() at both
  // start network event and end network event; otherwise, the WiFi link
  // statistics of the start network event is left in the list and matches
  // the wrong end network event.
  mockable void UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent network_event, const KeyValueStore& link_statistics);
  mockable void UpdateRtnlLinkStatistics(WiFi::NetworkEvent network_event,
                                         const old_rtnl_link_stats64& stats);

 private:
  friend class WiFiLinkStatisticsTest;

  // The snapshot of link statistics is updated if the network event is not
  // kUnknown. The difference between the end and start network events is
  // printed to the log if necessary, i.e., the end network event is a failure,
  // such as kDHCPFailure, kNetworkValidationFailure
  std::list<Nl80211LinkStatistics> nl80211_link_statistics_;
  std::list<RtnlLinkStatistics> rtnl_link_statistics_;
};

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_LINK_STATISTICS_H_
