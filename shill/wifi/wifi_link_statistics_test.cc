// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_link_statistics.h"

#include <memory>

#include <gmock/gmock.h>

#include <chromeos/dbus/service_constants.h>
#include "shill/mock_log.h"

using ::testing::_;
using ::testing::HasSubstr;

namespace shill {

class WiFiLinkStatisticsTest : public ::testing::Test {
 public:
  WiFiLinkStatisticsTest() : wifi_link_statistics_(new WiFiLinkStatistics()) {}
  ~WiFiLinkStatisticsTest() override = default;

 protected:
  void UpdateNl80211LinkStatistics(WiFi::NetworkEvent network_event,
                                   const KeyValueStore& link_statistics) {
    wifi_link_statistics_->UpdateNl80211LinkStatistics(network_event,
                                                       link_statistics);
  }

  void UpdateRtnlLinkStatistics(WiFi::NetworkEvent network_event,
                                const old_rtnl_link_stats64& stats) {
    wifi_link_statistics_->UpdateRtnlLinkStatistics(network_event, stats);
  }

 private:
  std::unique_ptr<WiFiLinkStatistics> wifi_link_statistics_;
};

TEST_F(WiFiLinkStatisticsTest, DhcpFailure) {
  ScopedMockLog log;
  KeyValueStore link_statistics;
  link_statistics.Set<uint32_t>(kPacketReceiveSuccessesProperty, 8);
  old_rtnl_link_stats64 stats;
  WiFi::NetworkEvent current_network_event =
      WiFi::NetworkEvent::kIPConfigurationStart;
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       HasSubstr(kPacketReceiveSuccessesProperty)))
      .Times(0);
  UpdateNl80211LinkStatistics(current_network_event, link_statistics);
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _, HasSubstr("rx_packets")))
      .Times(0);
  UpdateRtnlLinkStatistics(current_network_event, stats);
  current_network_event = WiFi::NetworkEvent::kDHCPFailure;
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       HasSubstr(kPacketReceiveSuccessesProperty)))
      .Times(1);
  UpdateNl80211LinkStatistics(current_network_event, link_statistics);
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _, HasSubstr("rx_packets")))
      .Times(1);
  UpdateRtnlLinkStatistics(current_network_event, stats);
}

TEST_F(WiFiLinkStatisticsTest, NetworkValidationFailure) {
  ScopedMockLog log;
  KeyValueStore link_statistics;
  link_statistics.Set<uint32_t>(kPacketReceiveSuccessesProperty, 8);
  old_rtnl_link_stats64 stats;
  WiFi::NetworkEvent current_network_event =
      WiFi::NetworkEvent::kNetworkValidationStart;
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       HasSubstr(kPacketReceiveSuccessesProperty)))
      .Times(0);
  UpdateNl80211LinkStatistics(current_network_event, link_statistics);
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _, HasSubstr("rx_packets")))
      .Times(0);
  UpdateRtnlLinkStatistics(current_network_event, stats);
  current_network_event = WiFi::NetworkEvent::kNetworkValidationFailure;
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       HasSubstr(kPacketReceiveSuccessesProperty)))
      .Times(1);
  UpdateNl80211LinkStatistics(current_network_event, link_statistics);
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _, HasSubstr("rx_packets")))
      .Times(1);
  UpdateRtnlLinkStatistics(current_network_event, stats);
}

}  // namespace shill
