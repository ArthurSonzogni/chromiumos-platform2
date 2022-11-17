// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_link_statistics.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include <base/strings/stringprintf.h>

#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>
#include "shill/metrics.h"
#include "shill/mock_log.h"

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::StrEq;

namespace shill {
namespace {

constexpr WiFiLinkStatistics::StationStats kDhcpStartNl80211Stats = {
    .tx_retries = 5,
    .tx_failed = 9,
    .rx_drop_misc = 15,
    .signal = -33,
    .signal_avg = -30,
    .rx = {.packets = 63, .bytes = 503},
    .tx = {.packets = 75, .bytes = 653}};
constexpr WiFiLinkStatistics::StationStats kDhcpEndNl80211Stats = {
    .tx_retries = 93,
    .tx_failed = 67,
    .rx_drop_misc = 153,
    .signal = -23,
    .signal_avg = -30,
    .rx = {.packets = 3587, .bytes = 52305},
    .tx = {.packets = 4163, .bytes = 56778}};
constexpr WiFiLinkStatistics::StationStats kDhcpDiffNl80211Stats = {
    .tx_retries = 88,
    .tx_failed = 58,
    .rx_drop_misc = 138,
    .signal = -23,
    .signal_avg = -30,
    .rx = {.packets = 3524, .bytes = 51802},
    .tx = {.packets = 4088, .bytes = 56125}};
constexpr WiFiLinkStatistics::StationStats kNetworkValidationStartNl80211Stats =
    {.tx_retries = 20,
     .tx_failed = 15,
     .rx_drop_misc = 37,
     .signal = -28,
     .signal_avg = -29,
     .rx = {.packets = 96, .bytes = 730},
     .tx = {.packets = 112, .bytes = 816}};
constexpr WiFiLinkStatistics::StationStats kNetworkValidationEndNl80211Stats = {
    .tx_retries = 88,
    .tx_failed = 56,
    .rx_drop_misc = 103,
    .signal = -27,
    .signal_avg = -30,
    .rx = {.packets = 3157, .bytes = 29676},
    .tx = {.packets = 3682, .bytes = 31233}};
constexpr WiFiLinkStatistics::StationStats kNetworkValidationDiffNl80211Stats =
    {.tx_retries = 68,
     .tx_failed = 41,
     .rx_drop_misc = 66,
     .signal = -27,
     .signal_avg = -30,
     .rx = {.packets = 3061, .bytes = 28946},
     .tx = {.packets = 3570, .bytes = 30417}};
constexpr old_rtnl_link_stats64 kDhcpStartRtnlStats = {
    17, 32, 105, 206, 3, 2, 8, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr old_rtnl_link_stats64 kDhcpEndRtnlStats = {
    3862, 3362, 49510, 43641, 35, 31, 29, 55, 0, 0, 0, 0,
    0,    0,    0,     0,     0,  0,  0,  0,  0, 0, 0};
constexpr old_rtnl_link_stats64 kDhcpDiffRtnlStats = {
    3845, 3330, 49405, 43435, 32, 29, 21, 49, 0, 0, 0, 0,
    0,    0,    0,     0,     0,  0,  0,  0,  0, 0, 0};
constexpr old_rtnl_link_stats64 kNetworkValidationStartRtnlStats = {
    29, 36, 278, 233, 6, 3, 11, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr old_rtnl_link_stats64 kNetworkValidationEndRtnlStats = {
    1509, 2022, 23890, 36217, 21, 26, 23, 31, 0, 0, 0, 0,
    0,    0,    0,     0,     0,  0,  0,  0,  0, 0, 0};
constexpr old_rtnl_link_stats64 kNetworkValidationDiffRtnlStats = {
    1480, 1986, 23612, 35984, 15, 23, 12, 22, 0, 0, 0, 0,
    0,    0,    0,     0,     0,  0,  0,  0,  0, 0, 0};

std::string Nl80211Log(WiFiLinkStatistics::Trigger start_event,
                       WiFiLinkStatistics::Trigger end_event,
                       const WiFiLinkStatistics::StationStats& diff_stats) {
  return "Network event related to NL80211 link statistics: " +
         WiFiLinkStatistics::LinkStatisticsTriggerToString(start_event) +
         " -> " + WiFiLinkStatistics::LinkStatisticsTriggerToString(end_event) +
         "; the NL80211 link statistics delta for the last 0 seconds is " +
         std::string(kPacketReceiveSuccessesProperty) + " " +
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

std::string RtnlLog(WiFiLinkStatistics::Trigger start_event,
                    WiFiLinkStatistics::Trigger end_event,
                    const old_rtnl_link_stats64& diff_stats) {
  return "Network event related to RTNL link statistics: " +
         WiFiLinkStatistics::LinkStatisticsTriggerToString(start_event) +
         " -> " + WiFiLinkStatistics::LinkStatisticsTriggerToString(end_event) +
         "; the RTNL link statistics delta for the last 0 seconds is " +
         "rx_packets " + std::to_string(diff_stats.rx_packets) +
         " tx_packets " + std::to_string(diff_stats.tx_packets) + " rx_bytes " +
         std::to_string(diff_stats.rx_bytes) + " tx_bytes " +
         std::to_string(diff_stats.tx_bytes) + " rx_errors " +
         std::to_string(diff_stats.rx_errors) + " tx_errors " +
         std::to_string(diff_stats.tx_errors) + " rx_dropped " +
         std::to_string(diff_stats.rx_dropped) + " tx_dropped " +
         std::to_string(diff_stats.tx_dropped);
}
}  // namespace

class WiFiLinkStatisticsTest : public ::testing::Test {
 public:
  WiFiLinkStatisticsTest() : wifi_link_statistics_(new WiFiLinkStatistics()) {}
  ~WiFiLinkStatisticsTest() override = default;

 protected:
  void UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger trigger,
      const WiFiLinkStatistics::StationStats& stats) {
    wifi_link_statistics_->UpdateNl80211LinkStatistics(trigger, stats);
  }

  void UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger trigger,
                                const old_rtnl_link_stats64& stats) {
    wifi_link_statistics_->UpdateRtnlLinkStatistics(trigger, stats);
  }

 private:
  std::unique_ptr<WiFiLinkStatistics> wifi_link_statistics_;
};

TEST_F(WiFiLinkStatisticsTest, DhcpFailure) {
  ScopedMockLog log;

  // IP configuration starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kIPConfigurationStart,
      kDhcpStartNl80211Stats);
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger::kIPConfigurationStart,
                           kDhcpStartRtnlStats);
  // DHCP failure
  EXPECT_CALL(
      log,
      Log(logging::LOGGING_INFO, _,
          StrEq(Nl80211Log(WiFiLinkStatistics::Trigger::kIPConfigurationStart,
                           WiFiLinkStatistics::Trigger::kDHCPFailure,
                           kDhcpDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(WiFiLinkStatistics::Trigger::kDHCPFailure,
                              kDhcpEndNl80211Stats);
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _,
               StrEq(RtnlLog(WiFiLinkStatistics::Trigger::kIPConfigurationStart,
                             WiFiLinkStatistics::Trigger::kDHCPFailure,
                             kDhcpDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger::kDHCPFailure,
                           kDhcpEndRtnlStats);
}

TEST_F(WiFiLinkStatisticsTest, NetworkValidationFailure) {
  ScopedMockLog log;

  // Network validation starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kNetworkValidationStart,
      kNetworkValidationStartNl80211Stats);
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger::kNetworkValidationStart,
                           kNetworkValidationStartRtnlStats);

  // Network validation failure
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _,
                  StrEq(Nl80211Log(
                      WiFiLinkStatistics::Trigger::kNetworkValidationStart,
                      WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
                      kNetworkValidationDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
      kNetworkValidationEndNl80211Stats);
  EXPECT_CALL(
      log,
      Log(logging::LOGGING_INFO, _,
          StrEq(RtnlLog(WiFiLinkStatistics::Trigger::kNetworkValidationStart,
                        WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
                        kNetworkValidationDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(
      WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
      kNetworkValidationEndRtnlStats);
}

TEST_F(WiFiLinkStatisticsTest, DhcpNetworkValidationFailures) {
  ScopedMockLog log;

  // IP configuration starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kIPConfigurationStart,
      kDhcpStartNl80211Stats);
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger::kIPConfigurationStart,
                           kDhcpStartRtnlStats);

  // Network validation starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kNetworkValidationStart,
      kNetworkValidationStartNl80211Stats);
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger::kNetworkValidationStart,
                           kNetworkValidationStartRtnlStats);

  // Network validation failure
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _,
                  StrEq(Nl80211Log(
                      WiFiLinkStatistics::Trigger::kNetworkValidationStart,
                      WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
                      kNetworkValidationDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
      kNetworkValidationEndNl80211Stats);
  EXPECT_CALL(
      log,
      Log(logging::LOGGING_INFO, _,
          StrEq(RtnlLog(WiFiLinkStatistics::Trigger::kNetworkValidationStart,
                        WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
                        kNetworkValidationDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(
      WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
      kNetworkValidationEndRtnlStats);

  // DHCP failure
  EXPECT_CALL(
      log,
      Log(logging::LOGGING_INFO, _,
          StrEq(Nl80211Log(WiFiLinkStatistics::Trigger::kIPConfigurationStart,
                           WiFiLinkStatistics::Trigger::kDHCPFailure,
                           kDhcpDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(WiFiLinkStatistics::Trigger::kDHCPFailure,
                              kDhcpEndNl80211Stats);
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _,
               StrEq(RtnlLog(WiFiLinkStatistics::Trigger::kIPConfigurationStart,
                             WiFiLinkStatistics::Trigger::kDHCPFailure,
                             kDhcpDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger::kDHCPFailure,
                           kDhcpEndRtnlStats);
}

TEST_F(WiFiLinkStatisticsTest, StationInfoTriggerConvert) {
  std::vector<WiFiLinkStatistics::Trigger> triggers = {
      WiFiLinkStatistics::Trigger::kUnknown,
      WiFiLinkStatistics::Trigger::kIPConfigurationStart,
      WiFiLinkStatistics::Trigger::kConnected,
      WiFiLinkStatistics::Trigger::kDHCPRenewOnRoam,
      WiFiLinkStatistics::Trigger::kDHCPSuccess,
      WiFiLinkStatistics::Trigger::kDHCPFailure,
      WiFiLinkStatistics::Trigger::kSlaacFinished,
      WiFiLinkStatistics::Trigger::kNetworkValidationStart,
      WiFiLinkStatistics::Trigger::kNetworkValidationSuccess,
      WiFiLinkStatistics::Trigger::kNetworkValidationFailure,
      WiFiLinkStatistics::Trigger::kCQMRSSILow,
      WiFiLinkStatistics::Trigger::kCQMRSSIHigh,
      WiFiLinkStatistics::Trigger::kCQMBeaconLoss,
      WiFiLinkStatistics::Trigger::kCQMPacketLoss,
      WiFiLinkStatistics::Trigger::kPeriodicCheck,
      WiFiLinkStatistics::Trigger::kBackground};

  std::vector<Metrics::WiFiLinkQualityTrigger> expected = {
      Metrics::kWiFiLinkQualityTriggerUnknown,
      Metrics::kWiFiLinkQualityTriggerIPConfigurationStart,
      Metrics::kWiFiLinkQualityTriggerConnected,
      Metrics::kWiFiLinkQualityTriggerDHCPRenewOnRoam,
      Metrics::kWiFiLinkQualityTriggerDHCPSuccess,
      Metrics::kWiFiLinkQualityTriggerDHCPFailure,
      Metrics::kWiFiLinkQualityTriggerSlaacFinished,
      Metrics::kWiFiLinkQualityTriggerNetworkValidationStart,
      Metrics::kWiFiLinkQualityTriggerNetworkValidationSuccess,
      Metrics::kWiFiLinkQualityTriggerNetworkValidationFailure,
      Metrics::kWiFiLinkQualityTriggerCQMRSSILow,
      Metrics::kWiFiLinkQualityTriggerCQMRSSIHigh,
      Metrics::kWiFiLinkQualityTriggerCQMBeaconLoss,
      Metrics::kWiFiLinkQualityTriggerCQMPacketLoss,
      Metrics::kWiFiLinkQualityTriggerPeriodicCheck,
      Metrics::kWiFiLinkQualityTriggerUnknown};

  EXPECT_EQ(triggers.size(), expected.size());
  std::vector<Metrics::WiFiLinkQualityTrigger> converted;
  for (auto trigger : triggers) {
    converted.push_back(
        WiFiLinkStatistics::ConvertLinkStatsTriggerEvent(trigger));
  }
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(), converted.begin()));
}

TEST_F(WiFiLinkStatisticsTest, StationInfoReportConvert) {
  // Assign an arbitrary value to the fields that are not yet supported by
  // the conversion method. That will make the test fail when the conversion
  // method starts handling those fields, which will ensure that the test also
  // gets updated to handle them.
  constexpr int64_t kNotHandledYet = 31;

  WiFiLinkStatistics::StationStats stats = {
      .tx_retries = 50,
      .tx_failed = 3,
      .rx_drop_misc = 5,
      .signal = kNotHandledYet,
      .signal_avg = kNotHandledYet,
      .rx =
          {
              .packets = 1500,
              .bytes = 8000,
              .bitrate = 100,
              .mcs = 9,
              .nss = 2,
              .dcm = kNotHandledYet,
          },
      .tx =
          {
              .packets = 1300,
              .bytes = 7000,
              .bitrate = 200,
              .mcs = 7,
              .nss = 2,
              .dcm = kNotHandledYet,
          },
  };

  Metrics::WiFiLinkQualityReport expected = {
      .tx_retries = 50,
      .tx_failures = 3,
      .rx_drops = 5,
      .rx =
          {
              .packets = 1500,
              .bytes = 8000,
              .bitrate = 100,
              .mcs = 9,
              .nss = 2,
          },
      .tx =
          {
              .packets = 1300,
              .bytes = 7000,
              .bitrate = 200,
              .mcs = 7,
              .nss = 2,
          },
  };

  std::vector<WiFiLinkStatistics::ChannelWidth> widths = {
      WiFiLinkStatistics::ChannelWidth::kChannelWidthUnknown,
      WiFiLinkStatistics::ChannelWidth::kChannelWidth20MHz,
      WiFiLinkStatistics::ChannelWidth::kChannelWidth40MHz,
      WiFiLinkStatistics::ChannelWidth::kChannelWidth80MHz,
      WiFiLinkStatistics::ChannelWidth::kChannelWidth80p80MHz,
      WiFiLinkStatistics::ChannelWidth::kChannelWidth160MHz,
      WiFiLinkStatistics::ChannelWidth::kChannelWidth320MHz,
  };
  std::vector<Metrics::WiFiChannelWidth> expected_widths = {
      Metrics::kWiFiChannelWidthUnknown,  Metrics::kWiFiChannelWidth20MHz,
      Metrics::kWiFiChannelWidth40MHz,    Metrics::kWiFiChannelWidth80MHz,
      Metrics::kWiFiChannelWidth80p80MHz, Metrics::kWiFiChannelWidth160MHz,
      Metrics::kWiFiChannelWidth320MHz,
  };
  EXPECT_EQ(widths.size(), expected_widths.size());

  WiFiLinkStatistics::StationStats s = stats;
  Metrics::WiFiLinkQualityReport e = expected;
  for (auto it = widths.begin(); it != widths.end(); ++it) {
    s.rx.width = *it;
    e.rx.width = expected_widths[it - widths.begin()];
    EXPECT_EQ(e, WiFiLinkStatistics::ConvertLinkStatsReport(s));
  }
  s = stats;
  e = expected;
  for (auto it = widths.begin(); it != widths.end(); ++it) {
    s.tx.width = *it;
    e.tx.width = expected_widths[it - widths.begin()];
    EXPECT_EQ(e, WiFiLinkStatistics::ConvertLinkStatsReport(s));
  }

  std::vector<WiFiLinkStatistics::LinkMode> modes = {
      WiFiLinkStatistics::LinkMode::kLinkModeUnknown,
      WiFiLinkStatistics::LinkMode::kLinkModeLegacy,
      WiFiLinkStatistics::LinkMode::kLinkModeVHT,
      WiFiLinkStatistics::LinkMode::kLinkModeHE,
      WiFiLinkStatistics::LinkMode::kLinkModeEHT,
  };
  std::vector<Metrics::WiFiLinkMode> expected_modes = {
      Metrics::kWiFiLinkModeUnknown, Metrics::kWiFiLinkModeLegacy,
      Metrics::kWiFiLinkModeVHT,     Metrics::kWiFiLinkModeHE,
      Metrics::kWiFiLinkModeEHT,
  };
  EXPECT_EQ(modes.size(), expected_modes.size());

  s = stats;
  e = expected;
  for (auto it = modes.begin(); it != modes.end(); ++it) {
    s.rx.mode = *it;
    e.rx.mode = expected_modes[it - modes.begin()];
    EXPECT_EQ(e, WiFiLinkStatistics::ConvertLinkStatsReport(s));
  }
  s = stats;
  e = expected;
  for (auto it = modes.begin(); it != modes.end(); ++it) {
    s.tx.mode = *it;
    e.tx.mode = expected_modes[it - modes.begin()];
    EXPECT_EQ(e, WiFiLinkStatistics::ConvertLinkStatsReport(s));
  }

  std::vector<WiFiLinkStatistics::GuardInterval> gi = {
      WiFiLinkStatistics::GuardInterval::kLinkStatsGIUnknown,
      WiFiLinkStatistics::GuardInterval::kLinkStatsGI_0_4,
      WiFiLinkStatistics::GuardInterval::kLinkStatsGI_0_8,
      WiFiLinkStatistics::GuardInterval::kLinkStatsGI_1_6,
      WiFiLinkStatistics::GuardInterval::kLinkStatsGI_3_2,
  };
  std::vector<Metrics::WiFiGuardInterval> expected_gi = {
      Metrics::kWiFiGuardIntervalUnknown, Metrics::kWiFiGuardInterval_0_4,
      Metrics::kWiFiGuardInterval_0_8,    Metrics::kWiFiGuardInterval_1_6,
      Metrics::kWiFiGuardInterval_3_2,
  };
  EXPECT_EQ(gi.size(), expected_gi.size());

  s = stats;
  e = expected;
  for (auto it = gi.begin(); it != gi.end(); ++it) {
    s.rx.gi = *it;
    e.rx.gi = expected_gi[it - gi.begin()];
    EXPECT_EQ(e, WiFiLinkStatistics::ConvertLinkStatsReport(s));
  }
  s = stats;
  e = expected;
  for (auto it = gi.begin(); it != gi.end(); ++it) {
    s.tx.gi = *it;
    e.tx.gi = expected_gi[it - gi.begin()];
    EXPECT_EQ(e, WiFiLinkStatistics::ConvertLinkStatsReport(s));
  }
}

}  // namespace shill
