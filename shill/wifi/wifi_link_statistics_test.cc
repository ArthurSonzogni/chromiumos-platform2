// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_link_statistics.h"

#include <memory>
#include <string>

#include <gmock/gmock.h>

#include <base/strings/stringprintf.h>

#include <chromeos/dbus/service_constants.h>
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

const KeyValueStore CreateNl80211LinkStatistics(
    const WiFiLinkStatistics::StationStats& nl80211_stats) {
  KeyValueStore link_statistics;
  link_statistics.Set<uint32_t>(kPacketReceiveSuccessesProperty,
                                nl80211_stats.rx.packets);
  link_statistics.Set<uint32_t>(kPacketTransmitSuccessesProperty,
                                nl80211_stats.tx.packets);
  link_statistics.Set<uint32_t>(kByteReceiveSuccessesProperty,
                                nl80211_stats.rx.bytes);
  link_statistics.Set<uint32_t>(kByteTransmitSuccessesProperty,
                                nl80211_stats.tx.bytes);
  link_statistics.Set<uint32_t>(kPacketTransmitFailuresProperty,
                                nl80211_stats.tx_failed);
  link_statistics.Set<uint32_t>(kTransmitRetriesProperty,
                                nl80211_stats.tx_retries);
  link_statistics.Set<uint64_t>(kPacketReceiveDropProperty,
                                nl80211_stats.rx_drop_misc);
  link_statistics.Set<int32_t>(kLastReceiveSignalDbmProperty,
                               nl80211_stats.signal);
  link_statistics.Set<int32_t>(kAverageReceiveSignalDbmProperty,
                               nl80211_stats.signal_avg);
  return link_statistics;
}
}  // namespace

class WiFiLinkStatisticsTest : public ::testing::Test {
 public:
  WiFiLinkStatisticsTest() : wifi_link_statistics_(new WiFiLinkStatistics()) {}
  ~WiFiLinkStatisticsTest() override = default;

 protected:
  void UpdateNl80211LinkStatistics(WiFiLinkStatistics::Trigger trigger,
                                   const KeyValueStore& link_statistics) {
    wifi_link_statistics_->UpdateNl80211LinkStatistics(trigger,
                                                       link_statistics);
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
      CreateNl80211LinkStatistics(kDhcpStartNl80211Stats));
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
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kDHCPFailure,
      CreateNl80211LinkStatistics(kDhcpEndNl80211Stats));
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
      CreateNl80211LinkStatistics(kNetworkValidationStartNl80211Stats));
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
      CreateNl80211LinkStatistics(kNetworkValidationEndNl80211Stats));
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
      CreateNl80211LinkStatistics(kDhcpStartNl80211Stats));
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
      CreateNl80211LinkStatistics(kNetworkValidationStartNl80211Stats));
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
      CreateNl80211LinkStatistics(kNetworkValidationEndNl80211Stats));
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
  UpdateNl80211LinkStatistics(
      WiFiLinkStatistics::Trigger::kDHCPFailure,
      CreateNl80211LinkStatistics(kDhcpEndNl80211Stats));
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _,
               StrEq(RtnlLog(WiFiLinkStatistics::Trigger::kIPConfigurationStart,
                             WiFiLinkStatistics::Trigger::kDHCPFailure,
                             kDhcpDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(WiFiLinkStatistics::Trigger::kDHCPFailure,
                           kDhcpEndRtnlStats);
}

}  // namespace shill
