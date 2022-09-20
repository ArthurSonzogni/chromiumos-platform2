// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_LINK_STATISTICS_H_
#define SHILL_WIFI_WIFI_LINK_STATISTICS_H_

#include <base/time/time.h>

#include "shill/mockable.h"
#include "shill/net/rtnl_link_stats.h"
#include "shill/store/key_value_store.h"
#include "shill/wifi/wifi.h"

namespace shill {

class WiFiLinkStatistics {
 public:
  struct Nl80211LinkStatistics {
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
  WiFiLinkStatistics() = default;
  virtual ~WiFiLinkStatistics() = default;
  // Update a new snapshot of WiFi link statistics.
  mockable void UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent network_event, const KeyValueStore& link_statistics);
  mockable void UpdateRtnlLinkStatistics(WiFi::NetworkEvent network_event,
                                         const old_rtnl_link_stats64& stats);

 private:
  friend class WiFiLinkStatisticsTest;
  // The last snapshot of WiFi link statistics
  old_rtnl_link_stats64 rtnl_link_statistics_;
  Nl80211LinkStatistics nl80211_link_statistics_;
  // The NetworkEvent at which the snapshot of WiFi link statistics was made
  WiFi::NetworkEvent rtnl_network_event_ = WiFi::NetworkEvent::kUnknown;
  WiFi::NetworkEvent nl80211_network_event_ = WiFi::NetworkEvent::kUnknown;
  // The timestamp at which the snapshot of WiFi link statistics was made
  base::Time rtnl_timestamp_;
  base::Time nl80211_timestamp_;
};

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_LINK_STATISTICS_H_
