// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_WIFI_LINK_STATISTICS_H_
#define SHILL_WIFI_MOCK_WIFI_LINK_STATISTICS_H_

#include <gmock/gmock.h>

#include "shill/wifi/wifi_link_statistics.h"

namespace shill {

class MockWiFiLinkStatistics : public WiFiLinkStatistics {
 public:
  MockWiFiLinkStatistics() = default;
  ~MockWiFiLinkStatistics() = default;

  MOCK_METHOD(void,
              UpdateNl80211LinkStatistics,
              (WiFi::LinkStatisticsTrigger trigger,
               const KeyValueStore& link_statistics),
              (override));
  MOCK_METHOD(void,
              UpdateRtnlLinkStatistics,
              (WiFi::LinkStatisticsTrigger trigger,
               const old_rtnl_link_stats64& stats),
              (override));
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_WIFI_LINK_STATISTICS_H_
