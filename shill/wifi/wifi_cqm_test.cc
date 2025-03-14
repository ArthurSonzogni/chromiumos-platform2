// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_cqm.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/netlink_packet.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/metrics.h"
#include "shill/mock_control.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi.h"
#include "shill/wifi/nl80211_message.h"
#include "shill/wifi/wifi.h"

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace shill {

namespace {

// Fake MAC address.
constexpr net_base::MacAddress kDeviceAddress(
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);
// Cooldown period for triggering firmware dumps configured by wifi_cqm.
constexpr auto kFwDumpCoolDownPeriod = base::Seconds(360);
const uint16_t kNl80211FamilyId = 0x13;

// Bytes representing NL80211 CQM message to pass to unit tests.
const uint8_t kCQMBeaconLossNLMsg[] = {
    0x2c, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x08, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x08, 0x00, 0x5e, 0x00, 0x04, 0x00, 0x08, 0x00};

const uint8_t kCQMRssiLowNLMsg[] = {
    0x38, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x5e, 0x00, 0x08, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x09, 0x00, 0xba, 0xff, 0xff, 0xff};

const uint8_t kCQMRssiHighNLMsg[] = {
    0x38, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x5e, 0x00, 0x08, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x09, 0x00, 0xd8, 0xff, 0xff, 0xff};

const uint8_t kCQMPacketLossNLMsg[] = {
    0x30, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x5e, 0x00, 0x08, 0x00, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00};

}  // namespace

class WiFiCQMTest : public ::testing::Test {
 public:
  WiFiCQMTest()
      : dispatcher_(std::make_unique<EventDispatcherForTest>()),
        manager_(&control_interface_, dispatcher_.get(), &metrics_),
        wifi_(new MockWiFi(
            &manager_, "wifi", kDeviceAddress, 0, 0, new MockWakeOnWiFi())),
        wifi_cqm_(new WiFiCQM(&metrics_, wifi().get())) {
    Nl80211Message::SetMessageType(kNl80211FamilyId);
  }

  ~WiFiCQMTest() override = default;

  std::unique_ptr<EventDispatcherForTest> dispatcher_;

 protected:
  MockMetrics* metrics() { return &metrics_; }

  void OnCQMNotify(const Nl80211Message& nl80211_message) {
    wifi_cqm_->OnCQMNotify(nl80211_message);
  }

  void TriggerFwDump() { return wifi_cqm_->TriggerFwDump(); }

  scoped_refptr<MockWiFi> wifi() { return wifi_; }

 private:
  MockMetrics metrics_;
  MockControl control_interface_;
  NiceMock<MockManager> manager_;

  scoped_refptr<MockWiFi> wifi_;
  std::unique_ptr<WiFiCQM> wifi_cqm_;
};

TEST_F(WiFiCQMTest, TriggerFwDump) {
  ScopedMockLog log;

  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);

  EXPECT_CALL(log, Log(_, _, HasSubstr("Triggering FW dump")));
  EXPECT_CALL(*wifi(), GenerateFirmwareDump());
  TriggerFwDump();
  Mock::VerifyAndClearExpectations(&log);
  Mock::VerifyAndClearExpectations(wifi().get());

  // No new firmware dump should be triggered in cool down period, e.g. we
  // forward time by half of the cooldown period.
  dispatcher_->task_environment().FastForwardBy(kFwDumpCoolDownPeriod / 2);
  EXPECT_CALL(*wifi(), GenerateFirmwareDump()).Times(0);
  TriggerFwDump();
  Mock::VerifyAndClearExpectations(wifi().get());

  // After the cooldown time, firmware dump can be triggered again.
  dispatcher_->task_environment().FastForwardBy(kFwDumpCoolDownPeriod);
  EXPECT_CALL(log, Log(_, _, HasSubstr("Triggering FW dump")));
  EXPECT_CALL(*wifi(), GenerateFirmwareDump());
  TriggerFwDump();
  Mock::VerifyAndClearExpectations(&log);
  Mock::VerifyAndClearExpectations(wifi().get());

  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiCQMTest, OnCQMNotificationBeaconLoss) {
  NotifyCqmMessage msg;

  ScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);

  net_base::NetlinkPacket packet(kCQMBeaconLossNLMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  const Nl80211Message& nl80211_msg =
      *reinterpret_cast<const Nl80211Message*>(&msg);

  EXPECT_CALL(log, Log(_, _, HasSubstr("Beacon loss observed")));
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricWiFiCQMNotification,
                                        Metrics::kWiFiCQMBeaconLoss, _));
  EXPECT_CALL(*wifi(), EmitStationInfoRequestEvent(
                           WiFiLinkStatistics::Trigger::kCQMBeaconLoss))
      .Times(1);
  OnCQMNotify(nl80211_msg);

  Mock::VerifyAndClearExpectations(wifi().get());
  Mock::VerifyAndClearExpectations(&log);

  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricWiFiCQMNotification,
                                        Metrics::kWiFiCQMBeaconLoss, _));
  EXPECT_CALL(*wifi(), EmitStationInfoRequestEvent(
                           WiFiLinkStatistics::Trigger::kCQMBeaconLoss))
      .Times(1);
  OnCQMNotify(nl80211_msg);
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiCQMTest, OnCQMNotificationLowRssiLevelBreach) {
  NotifyCqmMessage msg;
  ScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);
  net_base::NetlinkPacket packet(kCQMRssiLowNLMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  const Nl80211Message& nl80211_msg =
      *reinterpret_cast<const Nl80211Message*>(&msg);
  EXPECT_CALL(*wifi(), EmitStationInfoRequestEvent(
                           WiFiLinkStatistics::Trigger::kCQMRSSILow))
      .Times(1);
  OnCQMNotify(nl80211_msg);
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiCQMTest, OnCQMNotificationHighRssiLevelBreach) {
  NotifyCqmMessage msg;
  ScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);
  net_base::NetlinkPacket packet(kCQMRssiHighNLMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  const Nl80211Message& nl80211_msg =
      *reinterpret_cast<const Nl80211Message*>(&msg);
  EXPECT_CALL(*wifi(), EmitStationInfoRequestEvent(
                           WiFiLinkStatistics::Trigger::kCQMRSSIHigh))
      .Times(1);
  OnCQMNotify(nl80211_msg);
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiCQMTest, OnCQMNotificationPacketLoss) {
  NotifyCqmMessage msg;

  ScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);

  net_base::NetlinkPacket packet(kCQMPacketLossNLMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  const Nl80211Message& nl80211_msg =
      *reinterpret_cast<const Nl80211Message*>(&msg);

  EXPECT_CALL(log, Log(_, _, HasSubstr("Packet loss event received")));
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricWiFiCQMNotification,
                                        Metrics::kWiFiCQMPacketLoss, _));
  EXPECT_CALL(*wifi(), EmitStationInfoRequestEvent(
                           WiFiLinkStatistics::Trigger::kCQMPacketLoss))
      .Times(1);
  OnCQMNotify(nl80211_msg);

  Mock::VerifyAndClearExpectations(wifi().get());
  Mock::VerifyAndClearExpectations(&log);

  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricWiFiCQMNotification,
                                        Metrics::kWiFiCQMPacketLoss, _));
  EXPECT_CALL(*wifi(), EmitStationInfoRequestEvent(
                           WiFiLinkStatistics::Trigger::kCQMPacketLoss))
      .Times(1);
  OnCQMNotify(nl80211_msg);
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

}  // namespace shill
