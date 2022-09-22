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

constexpr nl80211_sta_info kDhcpStartNl80211Stats = {63, 75, 503, 653, 9,
                                                     5,  15, -33, -30};
constexpr nl80211_sta_info kDhcpEndNl80211Stats = {3587, 4163, 52305, 56778, 67,
                                                   93,   153,  -23,   -30};
constexpr nl80211_sta_info kDhcpDiffNl80211Stats = {
    3524, 4088, 51802, 56125, 58, 88, 138, -23, -30};
constexpr nl80211_sta_info kNetworkValidationStartNl80211Stats = {
    96, 112, 730, 816, 15, 20, 37, -28, -29};
constexpr nl80211_sta_info kNetworkValidationEndNl80211Stats = {
    3157, 3682, 29676, 31233, 56, 88, 103, -27, -30};
constexpr nl80211_sta_info kNetworkValidationDiffNl80211Stats = {
    3061, 3570, 28946, 30417, 41, 68, 66, -27, -30};
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

std::string Nl80211Log(WiFi::NetworkEvent start_event,
                       WiFi::NetworkEvent end_event,
                       const nl80211_sta_info& diff_stats) {
  return "Network event related to NL80211 link statistics: " +
         WiFiLinkStatistics::NetworkEventToString(start_event) + " -> " +
         WiFiLinkStatistics::NetworkEventToString(end_event) +
         "; the NL80211 link statistics delta for the last 0 seconds is " +
         std::string(kPacketReceiveSuccessesProperty) + " " +
         std::to_string(diff_stats.rx_packets_success) + " " +
         kPacketTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx_packets_success) + " " +
         kByteReceiveSuccessesProperty + " " +
         std::to_string(diff_stats.rx_bytes_success) + " " +
         kByteTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx_bytes_success) + " " +
         kPacketTransmitFailuresProperty + " " +
         std::to_string(diff_stats.tx_packets_failure) + " " +
         kTransmitRetriesProperty + " " +
         std::to_string(diff_stats.tx_retries) + " " +
         kPacketReceiveDropProperty + " " +
         std::to_string(diff_stats.rx_packets_dropped) +
         "; the current signal information: " + kLastReceiveSignalDbmProperty +
         " " + std::to_string(diff_stats.last_rx_signal_dbm) + " " +
         kAverageReceiveSignalDbmProperty + " " +
         std::to_string(diff_stats.avg_rx_signal_dbm);
}

std::string RtnlLog(WiFi::NetworkEvent start_event,
                    WiFi::NetworkEvent end_event,
                    const old_rtnl_link_stats64& diff_stats) {
  return "Network event related to RTNL link statistics: " +
         WiFiLinkStatistics::NetworkEventToString(start_event) + " -> " +
         WiFiLinkStatistics::NetworkEventToString(end_event) +
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
    const nl80211_sta_info& nl80211_stats) {
  KeyValueStore link_statistics;
  link_statistics.Set<uint32_t>(kPacketReceiveSuccessesProperty,
                                nl80211_stats.rx_packets_success);
  link_statistics.Set<uint32_t>(kPacketTransmitSuccessesProperty,
                                nl80211_stats.tx_packets_success);
  link_statistics.Set<uint32_t>(kByteReceiveSuccessesProperty,
                                nl80211_stats.rx_bytes_success);
  link_statistics.Set<uint32_t>(kByteTransmitSuccessesProperty,
                                nl80211_stats.tx_bytes_success);
  link_statistics.Set<uint32_t>(kPacketTransmitFailuresProperty,
                                nl80211_stats.tx_packets_failure);
  link_statistics.Set<uint32_t>(kTransmitRetriesProperty,
                                nl80211_stats.tx_retries);
  link_statistics.Set<uint64_t>(kPacketReceiveDropProperty,
                                nl80211_stats.rx_packets_dropped);
  link_statistics.Set<int32_t>(kLastReceiveSignalDbmProperty,
                               nl80211_stats.last_rx_signal_dbm);
  link_statistics.Set<int32_t>(kAverageReceiveSignalDbmProperty,
                               nl80211_stats.avg_rx_signal_dbm);
  return link_statistics;
}
}  // namespace

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

  // IP configuration starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kIPConfigurationStart,
      CreateNl80211LinkStatistics(kDhcpStartNl80211Stats));
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kIPConfigurationStart,
                           kDhcpStartRtnlStats);
  // DHCP failure
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _,
                  StrEq(Nl80211Log(WiFi::NetworkEvent::kIPConfigurationStart,
                                   WiFi::NetworkEvent::kDHCPFailure,
                                   kDhcpDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kDHCPFailure,
      CreateNl80211LinkStatistics(kDhcpEndNl80211Stats));
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       StrEq(RtnlLog(WiFi::NetworkEvent::kIPConfigurationStart,
                                     WiFi::NetworkEvent::kDHCPFailure,
                                     kDhcpDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kDHCPFailure, kDhcpEndRtnlStats);
}

TEST_F(WiFiLinkStatisticsTest, NetworkValidationFailure) {
  ScopedMockLog log;

  // Network validation starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kNetworkValidationStart,
      CreateNl80211LinkStatistics(kNetworkValidationStartNl80211Stats));
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kNetworkValidationStart,
                           kNetworkValidationStartRtnlStats);

  // Network validation failure
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _,
               StrEq(Nl80211Log(WiFi::NetworkEvent::kNetworkValidationStart,
                                WiFi::NetworkEvent::kNetworkValidationFailure,
                                kNetworkValidationDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kNetworkValidationFailure,
      CreateNl80211LinkStatistics(kNetworkValidationEndNl80211Stats));
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _,
                  StrEq(RtnlLog(WiFi::NetworkEvent::kNetworkValidationStart,
                                WiFi::NetworkEvent::kNetworkValidationFailure,
                                kNetworkValidationDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kNetworkValidationFailure,
                           kNetworkValidationEndRtnlStats);
}

TEST_F(WiFiLinkStatisticsTest, DhcpNetworkValidationFailures) {
  ScopedMockLog log;

  // IP configuration starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kIPConfigurationStart,
      CreateNl80211LinkStatistics(kDhcpStartNl80211Stats));
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kIPConfigurationStart,
                           kDhcpStartRtnlStats);

  // Network validation starts
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("NL80211 link statistics")))
      .Times(0);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kNetworkValidationStart,
      CreateNl80211LinkStatistics(kNetworkValidationStartNl80211Stats));
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _, HasSubstr("RTNL link statistics")))
      .Times(0);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kNetworkValidationStart,
                           kNetworkValidationStartRtnlStats);

  // Network validation failure
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _,
               StrEq(Nl80211Log(WiFi::NetworkEvent::kNetworkValidationStart,
                                WiFi::NetworkEvent::kNetworkValidationFailure,
                                kNetworkValidationDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kNetworkValidationFailure,
      CreateNl80211LinkStatistics(kNetworkValidationEndNl80211Stats));
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _,
                  StrEq(RtnlLog(WiFi::NetworkEvent::kNetworkValidationStart,
                                WiFi::NetworkEvent::kNetworkValidationFailure,
                                kNetworkValidationDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kNetworkValidationFailure,
                           kNetworkValidationEndRtnlStats);

  // DHCP failure
  EXPECT_CALL(log,
              Log(logging::LOGGING_INFO, _,
                  StrEq(Nl80211Log(WiFi::NetworkEvent::kIPConfigurationStart,
                                   WiFi::NetworkEvent::kDHCPFailure,
                                   kDhcpDiffNl80211Stats))))
      .Times(1);
  UpdateNl80211LinkStatistics(
      WiFi::NetworkEvent::kDHCPFailure,
      CreateNl80211LinkStatistics(kDhcpEndNl80211Stats));
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       StrEq(RtnlLog(WiFi::NetworkEvent::kIPConfigurationStart,
                                     WiFi::NetworkEvent::kDHCPFailure,
                                     kDhcpDiffRtnlStats))))
      .Times(1);
  UpdateRtnlLinkStatistics(WiFi::NetworkEvent::kDHCPFailure, kDhcpEndRtnlStats);
}

}  // namespace shill
