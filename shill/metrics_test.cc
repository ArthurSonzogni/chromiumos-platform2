// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/metrics.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <metrics/structured/event_base.h>
#include <metrics/structured/mock_recorder.h>
#include <metrics/structured/recorder_singleton.h>
#include <metrics/structured_events.h>
#include <metrics/timer_mock.h>

#include "shill/mock_control.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_service.h"
#include "shill/net/ieee80211.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/vpn_types.h"
#include "shill/wifi/mock_wifi.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
using testing::Ge;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::Test;

namespace shill {

class MetricsTest : public Test {
 public:
  MetricsTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        recorder_(new metrics::structured::MockRecorder()),
        service_(new MockService(&manager_)) {}

  ~MetricsTest() override = default;

  void SetUp() override {
    metrics_.SetLibraryForTesting(&library_);
    auto recorder =
        std::unique_ptr<metrics::structured::MockRecorder>(recorder_);
    metrics::structured::RecorderSingleton::GetInstance()->SetRecorderForTest(
        std::move(recorder));
  }

  void TearDown() override {
    // Destroy the MockRecorder object explicitly to trigger the verification.
    metrics::structured::RecorderSingleton::GetInstance()->SetRecorderForTest(
        nullptr);
  }

 protected:
  MockControl control_interface_;
  EventDispatcherForTest dispatcher_;
  MockManager manager_;
  Metrics metrics_;  // This must be destroyed after all |service_|s.
  MetricsLibraryMock library_;
  metrics::structured::MockRecorder* recorder_;
  const std::vector<uint8_t> ssid_;
  scoped_refptr<MockService> service_;
};

TEST_F(MetricsTest, EnumMetric) {
  Metrics::EnumMetric<Metrics::FixedName> metric1 = {
      .n = Metrics::FixedName{"Fake.Metric"},
      .max = 25,
  };
  EXPECT_CALL(library_, SendEnumToUMA("Fake.Metric", 10, 25));
  metrics_.SendEnumToUMA(metric1, 10);
  Mock::VerifyAndClearExpectations(&library_);

  Metrics::EnumMetric<Metrics::NameByTechnology> metric2 = {
      .n = Metrics::NameByTechnology{"FakeEnum"},
      .max = 13,
  };
  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Wifi.FakeEnum", 3, 13));
  metrics_.SendEnumToUMA(metric2, Technology::kWiFi, 3);
  Mock::VerifyAndClearExpectations(&library_);
  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Vpn.FakeEnum", 8, 13));
  metrics_.SendEnumToUMA(metric2, Technology::kVPN, 8);
  Mock::VerifyAndClearExpectations(&library_);

  Metrics::EnumMetric<Metrics::NameByTechnology> metric3 = {
      .n = Metrics::NameByTechnology{"FakeEnum",
                                     Metrics::TechnologyLocation::kAfterName},
      .max = 13,
  };
  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.FakeEnum.Wifi", 3, 13));
  metrics_.SendEnumToUMA(metric3, Technology::kWiFi, 3);
  Mock::VerifyAndClearExpectations(&library_);

  Metrics::EnumMetric<Metrics::NameByVPNType> metric4 = {
      .n = Metrics::NameByVPNType{"Enum"},
      .max = 10,
  };
  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Vpn.ARC.Enum", 5, 10));
  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Vpn.Ikev2.Enum", 4, 10));
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.Vpn.L2tpIpsec.Enum", 3, 10));
  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Vpn.OpenVPN.Enum", 2, 10));
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.Vpn.ThirdParty.Enum", 1, 10));
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.Vpn.WireGuard.Enum", 0, 10));
  metrics_.SendEnumToUMA(metric4, VPNType::kARC, 5);
  metrics_.SendEnumToUMA(metric4, VPNType::kIKEv2, 4);
  metrics_.SendEnumToUMA(metric4, VPNType::kL2TPIPsec, 3);
  metrics_.SendEnumToUMA(metric4, VPNType::kOpenVPN, 2);
  metrics_.SendEnumToUMA(metric4, VPNType::kThirdParty, 1);
  metrics_.SendEnumToUMA(metric4, VPNType::kWireGuard, 0);
  Mock::VerifyAndClearExpectations(&library_);
}

TEST_F(MetricsTest, HistogramMetric) {
  Metrics::HistogramMetric<Metrics::FixedName> metric1 = {
      .n = Metrics::FixedName{"Fake.Histogram"},
      .min = 11,
      .max = 66,
      .num_buckets = 32,
  };
  EXPECT_CALL(library_, SendToUMA("Fake.Histogram", 23, 11, 66, 32));
  metrics_.SendToUMA(metric1, 23);
  Mock::VerifyAndClearExpectations(&library_);

  Metrics::HistogramMetric<Metrics::NameByTechnology> metric2 = {
      .n = Metrics::NameByTechnology{"FakeBuckets"},
      .min = 0,
      .max = 250,
      .num_buckets = 64,
  };
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Wifi.FakeBuckets", 148, 0, 250, 64));
  metrics_.SendToUMA(metric2, Technology::kWiFi, 148);
  Mock::VerifyAndClearExpectations(&library_);
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Ethernet.FakeBuckets", 13, 0, 250, 64));
  metrics_.SendToUMA(metric2, Technology::kEthernet, 13);
  Mock::VerifyAndClearExpectations(&library_);

  const Metrics::HistogramMetric<Metrics::NameByTechnology> metric3 = {
      .n = Metrics::NameByTechnology{"FakeBuckets",
                                     Metrics::TechnologyLocation::kAfterName},
      .min = 0,
      .max = 250,
      .num_buckets = 64,
  };
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.FakeBuckets.Wifi", 148, 0, 250, 64));
  metrics_.SendToUMA(metric3, Technology::kWiFi, 148);
  Mock::VerifyAndClearExpectations(&library_);
}

TEST_F(MetricsTest, SparseMetric) {
  Metrics::SparseMetric<Metrics::FixedName> metric1 = {
      .n = Metrics::FixedName{"Fake.SparseHistogram"},
  };
  EXPECT_CALL(library_, SendSparseToUMA("Fake.SparseHistogram", 123456));
  metrics_.SendSparseToUMA(metric1, 123456);
  Mock::VerifyAndClearExpectations(&library_);

  Metrics::SparseMetric<Metrics::NameByTechnology> metric2 = {
      .n = Metrics::NameByTechnology{"FakeBucket"},
  };
  EXPECT_CALL(library_, SendSparseToUMA("Network.Shill.Wifi.FakeBucket", 7890));
  metrics_.SendSparseToUMA(metric2, Technology::kWiFi, 7890);
  Mock::VerifyAndClearExpectations(&library_);
  EXPECT_CALL(library_,
              SendSparseToUMA("Network.Shill.Ethernet.FakeBucket", 123));
  metrics_.SendSparseToUMA(metric2, Technology::kEthernet, 123);
  Mock::VerifyAndClearExpectations(&library_);

  const Metrics::SparseMetric<Metrics::NameByTechnology> metric3 = {
      .n = Metrics::NameByTechnology{"FakePrefix",
                                     Metrics::TechnologyLocation::kAfterName},
  };
  EXPECT_CALL(library_, SendSparseToUMA("Network.Shill.FakePrefix.Wifi", 3456));
  metrics_.SendSparseToUMA(metric3, Technology::kWiFi, 3456);
  Mock::VerifyAndClearExpectations(&library_);
}

TEST_F(MetricsTest, FrequencyToChannel) {
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(2411));
  EXPECT_EQ(Metrics::kWiFiChannel2412, metrics_.WiFiFrequencyToChannel(2412));
  EXPECT_EQ(Metrics::kWiFiChannel2472, metrics_.WiFiFrequencyToChannel(2472));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(2473));
  EXPECT_EQ(Metrics::kWiFiChannel2484, metrics_.WiFiFrequencyToChannel(2484));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5169));
  EXPECT_EQ(Metrics::kWiFiChannel5170, metrics_.WiFiFrequencyToChannel(5170));
  EXPECT_EQ(Metrics::kWiFiChannel5190, metrics_.WiFiFrequencyToChannel(5190));
  EXPECT_EQ(Metrics::kWiFiChannel5180, metrics_.WiFiFrequencyToChannel(5180));
  EXPECT_EQ(Metrics::kWiFiChannel5200, metrics_.WiFiFrequencyToChannel(5200));
  EXPECT_EQ(Metrics::kWiFiChannel5230, metrics_.WiFiFrequencyToChannel(5230));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5231));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5239));
  EXPECT_EQ(Metrics::kWiFiChannel5240, metrics_.WiFiFrequencyToChannel(5240));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5241));
  EXPECT_EQ(Metrics::kWiFiChannel5320, metrics_.WiFiFrequencyToChannel(5320));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5321));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5499));
  EXPECT_EQ(Metrics::kWiFiChannel5500, metrics_.WiFiFrequencyToChannel(5500));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5501));
  EXPECT_EQ(Metrics::kWiFiChannel5700, metrics_.WiFiFrequencyToChannel(5700));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5701));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5744));
  EXPECT_EQ(Metrics::kWiFiChannel5745, metrics_.WiFiFrequencyToChannel(5745));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5746));
  EXPECT_EQ(Metrics::kWiFiChannel5825, metrics_.WiFiFrequencyToChannel(5825));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5826));
  EXPECT_EQ(Metrics::kWiFiChannel5955, metrics_.WiFiFrequencyToChannel(5955));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(5956));
  EXPECT_EQ(Metrics::kWiFiChannel7115, metrics_.WiFiFrequencyToChannel(7115));
  EXPECT_EQ(Metrics::kWiFiChannelUndef, metrics_.WiFiFrequencyToChannel(7116));
}

TEST_F(MetricsTest, ChannelToFrequencyRange) {
  EXPECT_EQ(Metrics::kWiFiFrequencyRangeUndef,
            metrics_.WiFiChannelToFrequencyRange(Metrics::kWiFiChannelUndef));
  EXPECT_EQ(Metrics::kWiFiFrequencyRange24,
            metrics_.WiFiChannelToFrequencyRange(Metrics::kWiFiChannel2484));
  EXPECT_EQ(Metrics::kWiFiFrequencyRange5,
            metrics_.WiFiChannelToFrequencyRange(Metrics::kWiFiChannel5620));
  EXPECT_EQ(Metrics::kWiFiFrequencyRange6,
            metrics_.WiFiChannelToFrequencyRange(Metrics::kWiFiChannel6255));
}

TEST_F(MetricsTest, TimeToConnect) {
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Cellular.TimeToConnect", Ge(0),
                        Metrics::kMetricTimeToConnectMillisecondsMin,
                        Metrics::kMetricTimeToConnectMillisecondsMax,
                        Metrics::kMetricTimeToConnectMillisecondsNumBuckets));
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  metrics_.NotifyDeviceConnectStarted(kInterfaceIndex);
  metrics_.NotifyDeviceConnectFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, TimeToDisable) {
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Cellular.TimeToDisable", Ge(0),
                        Metrics::kMetricTimeToDisableMillisecondsMin,
                        Metrics::kMetricTimeToDisableMillisecondsMax,
                        Metrics::kMetricTimeToDisableMillisecondsNumBuckets));
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  metrics_.NotifyDeviceDisableStarted(kInterfaceIndex);
  metrics_.NotifyDeviceDisableFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, TimeToEnable) {
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Cellular.TimeToEnable", Ge(0),
                        Metrics::kMetricTimeToEnableMillisecondsMin,
                        Metrics::kMetricTimeToEnableMillisecondsMax,
                        Metrics::kMetricTimeToEnableMillisecondsNumBuckets));
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  metrics_.NotifyDeviceEnableStarted(kInterfaceIndex);
  metrics_.NotifyDeviceEnableFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, TimeToInitialize) {
  EXPECT_CALL(
      library_,
      SendToUMA("Network.Shill.Cellular.TimeToInitialize", Ge(0),
                Metrics::kMetricTimeToInitializeMillisecondsMin,
                Metrics::kMetricTimeToInitializeMillisecondsMax,
                Metrics::kMetricTimeToInitializeMillisecondsNumBuckets));
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  metrics_.NotifyDeviceInitialized(kInterfaceIndex);
}

TEST_F(MetricsTest, TimeToScan) {
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Cellular.TimeToScan", Ge(0),
                        Metrics::kMetricTimeToScanMillisecondsMin,
                        Metrics::kMetricTimeToScanMillisecondsMax,
                        Metrics::kMetricTimeToScanMillisecondsNumBuckets));
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  metrics_.NotifyDeviceScanStarted(kInterfaceIndex);
  metrics_.NotifyDeviceScanFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, TimeToScanAndConnect) {
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Wifi.TimeToScan", Ge(0),
                        Metrics::kMetricTimeToScanMillisecondsMin,
                        Metrics::kMetricTimeToScanMillisecondsMax,
                        Metrics::kMetricTimeToScanMillisecondsNumBuckets));
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kWiFi);
  metrics_.NotifyDeviceScanStarted(kInterfaceIndex);
  metrics_.NotifyDeviceScanFinished(kInterfaceIndex);

  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Wifi.TimeToConnect", Ge(0),
                        Metrics::kMetricTimeToConnectMillisecondsMin,
                        Metrics::kMetricTimeToConnectMillisecondsMax,
                        Metrics::kMetricTimeToConnectMillisecondsNumBuckets));
  EXPECT_CALL(
      library_,
      SendToUMA("Network.Shill.Wifi.TimeToScanAndConnect", Ge(0),
                Metrics::kMetricTimeToScanMillisecondsMin,
                Metrics::kMetricTimeToScanMillisecondsMax +
                    Metrics::kMetricTimeToConnectMillisecondsMax,
                Metrics::kMetricTimeToScanMillisecondsNumBuckets +
                    Metrics::kMetricTimeToConnectMillisecondsNumBuckets));
  metrics_.NotifyDeviceConnectStarted(kInterfaceIndex);
  metrics_.NotifyDeviceConnectFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, SpontaneousConnect) {
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kWiFi);
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Wifi.TimeToConnect", Ge(0),
                        Metrics::kMetricTimeToConnectMillisecondsMin,
                        Metrics::kMetricTimeToConnectMillisecondsMax,
                        Metrics::kMetricTimeToConnectMillisecondsNumBuckets))
      .Times(0);
  EXPECT_CALL(
      library_,
      SendToUMA("Network.Shill.Wifi.TimeToScanAndConnect", Ge(0),
                Metrics::kMetricTimeToScanMillisecondsMin,
                Metrics::kMetricTimeToScanMillisecondsMax +
                    Metrics::kMetricTimeToConnectMillisecondsMax,
                Metrics::kMetricTimeToScanMillisecondsNumBuckets +
                    Metrics::kMetricTimeToConnectMillisecondsNumBuckets))
      .Times(0);
  // This simulates a connection that is not scan-based.
  metrics_.NotifyDeviceConnectFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, ResetConnectTimer) {
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kWiFi);
  chromeos_metrics::TimerReporterMock* mock_scan_timer =
      new chromeos_metrics::TimerReporterMock;
  metrics_.set_time_to_scan_timer(kInterfaceIndex, mock_scan_timer);
  chromeos_metrics::TimerReporterMock* mock_connect_timer =
      new chromeos_metrics::TimerReporterMock;
  metrics_.set_time_to_connect_timer(kInterfaceIndex, mock_connect_timer);
  chromeos_metrics::TimerReporterMock* mock_scan_connect_timer =
      new chromeos_metrics::TimerReporterMock;
  metrics_.set_time_to_scan_connect_timer(kInterfaceIndex,
                                          mock_scan_connect_timer);
  EXPECT_CALL(*mock_scan_timer, Reset()).Times(0);
  EXPECT_CALL(*mock_connect_timer, Reset());
  EXPECT_CALL(*mock_scan_connect_timer, Reset());
  metrics_.ResetConnectTimer(kInterfaceIndex);
}

TEST_F(MetricsTest, TimeToScanNoStart) {
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Cellular.TimeToScan", _, _, _, _))
      .Times(0);
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  metrics_.NotifyDeviceScanFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, TimeFromRekeyToFailureExceedMaxDuration) {
  chromeos_metrics::TimerReporterMock* mock_rekey_timer =
      new chromeos_metrics::TimerReporterMock;
  base::TimeDelta large_time_delta =
      base::Seconds(Metrics::kMetricTimeFromRekeyToFailureSeconds.max + 1);
  EXPECT_CALL(*mock_rekey_timer, HasStarted())
      .Times(2)
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_rekey_timer, Start());
  EXPECT_CALL(*mock_rekey_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(large_time_delta), Return(true)));
  EXPECT_CALL(library_, SendToUMA(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*mock_rekey_timer, Reset());
  metrics_.set_time_between_rekey_and_connection_failure_timer(
      mock_rekey_timer);
  metrics_.NotifyRekeyStart();
  metrics_.NotifyWiFiConnectionUnreliable();
}

TEST_F(MetricsTest, TimeFromRekeyToFailureValidDuration) {
  chromeos_metrics::TimerReporterMock* mock_rekey_timer =
      new chromeos_metrics::TimerReporterMock;
  base::TimeDelta good_time_delta =
      base::Seconds(Metrics::kMetricTimeFromRekeyToFailureSeconds.min + 1);
  EXPECT_CALL(*mock_rekey_timer, HasStarted())
      .Times(2)
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_rekey_timer, Start());
  EXPECT_CALL(*mock_rekey_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(good_time_delta), Return(true)));
  EXPECT_CALL(
      library_,
      SendToUMA("Network.Shill.WiFi.TimeFromRekeyToFailureSeconds", _, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_rekey_timer, Reset());
  metrics_.set_time_between_rekey_and_connection_failure_timer(
      mock_rekey_timer);
  metrics_.NotifyRekeyStart();
  metrics_.NotifyWiFiConnectionUnreliable();
}

TEST_F(MetricsTest, TimeFromRekeyToFailureBSSIDChange) {
  chromeos_metrics::TimerReporterMock* mock_rekey_timer =
      new chromeos_metrics::TimerReporterMock;

  EXPECT_CALL(*mock_rekey_timer, HasStarted())
      .Times(2)
      .WillOnce(Return(false))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_rekey_timer, Start());
  EXPECT_CALL(*mock_rekey_timer, Reset());
  EXPECT_CALL(*mock_rekey_timer, GetElapsedTime(_)).Times(0);
  EXPECT_CALL(
      library_,
      SendToUMA("Network.Shill.WiFi.TimeFromRekeyToFailureSeconds", _, _, _, _))
      .Times(0);
  metrics_.set_time_between_rekey_and_connection_failure_timer(
      mock_rekey_timer);
  metrics_.NotifyRekeyStart();
  metrics_.NotifyBSSIDChanged();
  metrics_.NotifyWiFiConnectionUnreliable();
}

TEST_F(MetricsTest, TimeToScanIgnore) {
  // Make sure TimeToScan is not sent if the elapsed time exceeds the max
  // value.  This simulates the case where the device is in an area with no
  // service.
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  base::TimeDelta large_time_delta =
      base::Milliseconds(Metrics::kMetricTimeToScanMillisecondsMax + 1);
  chromeos_metrics::TimerReporterMock* mock_time_to_scan_timer =
      new chromeos_metrics::TimerReporterMock;
  metrics_.set_time_to_scan_timer(kInterfaceIndex, mock_time_to_scan_timer);
  EXPECT_CALL(*mock_time_to_scan_timer, Stop()).WillOnce(Return(true));
  EXPECT_CALL(*mock_time_to_scan_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(large_time_delta), Return(true)));
  EXPECT_CALL(library_, SendToUMA(_, _, _, _, _)).Times(0);
  metrics_.NotifyDeviceScanStarted(kInterfaceIndex);
  metrics_.NotifyDeviceScanFinished(kInterfaceIndex);
}

TEST_F(MetricsTest, ReportDeviceScanResultToUma) {
  Metrics::WiFiScanResult result =
      Metrics::kScanResultProgressiveAndFullConnected;
  EXPECT_CALL(library_,
              SendEnumToUMA(Eq(Metrics::kMetricScanResult.n.name),
                            Metrics::kScanResultProgressiveAndFullConnected,
                            Metrics::kScanResultMax));
  metrics_.ReportDeviceScanResultToUma(result);
}

TEST_F(MetricsTest, CellularDrop) {
  static const char* const kUMATechnologyStrings[] = {
      kNetworkTechnology1Xrtt,    kNetworkTechnologyEdge,
      kNetworkTechnologyEvdo,     kNetworkTechnologyGprs,
      kNetworkTechnologyGsm,      kNetworkTechnologyHspa,
      kNetworkTechnologyHspaPlus, kNetworkTechnologyLte,
      kNetworkTechnologyUmts,     "Unknown",
      kNetworkTechnology5gNr};

  const uint16_t signal_strength = 100;
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  for (size_t index = 0; index < std::size(kUMATechnologyStrings); ++index) {
    EXPECT_CALL(library_,
                SendEnumToUMA(Eq(Metrics::kMetricCellularDrop.n.name), index,
                              Metrics::kCellularDropTechnologyMax));
    EXPECT_CALL(
        library_,
        SendToUMA(
            Eq(Metrics::kMetricCellularSignalStrengthBeforeDrop.n.name),
            signal_strength,
            Metrics::kMetricCellularSignalStrengthBeforeDrop.min,
            Metrics::kMetricCellularSignalStrengthBeforeDrop.max,
            Metrics::kMetricCellularSignalStrengthBeforeDrop.num_buckets));
    metrics_.NotifyCellularDeviceDrop(kUMATechnologyStrings[index],
                                      signal_strength);
    Mock::VerifyAndClearExpectations(&library_);
  }
}

TEST_F(MetricsTest, NotifyCellularConnectionResult_Default_Valid) {
  Error::Type error = Error::Type::kOperationFailed;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          "Network.Shill.Cellular.ConnectResult.DEFAULT",
          static_cast<int>(Metrics::CellularConnectResult::
                               kCellularConnectResultOperationFailed),
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultMax)));
  metrics_.NotifyCellularConnectionResult(
      error, Metrics::DetailedCellularConnectionResult::APNType::kDefault);
}

TEST_F(MetricsTest, NotifyCellularConnectionResult_Default_Unknown) {
  Error::Type invalid_error = Error::Type::kNumErrors;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          "Network.Shill.Cellular.ConnectResult.DEFAULT",
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultUnknown),
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultMax)));
  metrics_.NotifyCellularConnectionResult(
      invalid_error,
      Metrics::DetailedCellularConnectionResult::APNType::kDefault);
}

TEST_F(MetricsTest, NotifyCellularConnectionResult_Dun_Valid) {
  Error::Type error = Error::Type::kOperationFailed;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          "Network.Shill.Cellular.ConnectResult.DUN",
          static_cast<int>(Metrics::CellularConnectResult::
                               kCellularConnectResultOperationFailed),
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultMax)));
  metrics_.NotifyCellularConnectionResult(
      error, Metrics::DetailedCellularConnectionResult::APNType::kDUN);
}

TEST_F(MetricsTest, NotifyCellularConnectionResult_Dun_Unknown) {
  Error::Type invalid_error = Error::Type::kNumErrors;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          "Network.Shill.Cellular.ConnectResult.DUN",
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultUnknown),
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultMax)));
  metrics_.NotifyCellularConnectionResult(
      invalid_error, Metrics::DetailedCellularConnectionResult::APNType::kDUN);
}

TEST_F(MetricsTest, IntGid1) {
  std::optional<uint64_t> val;
  val = metrics_.IntGid1("123456");
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 0x123456);
  val = metrics_.IntGid1("ABC123456");
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 0xABC123456);
  val = metrics_.IntGid1("FFFFFFFFFFFFFFF");  // 15 digits
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 0xFFFFFFFFFFFFFFF);
  val = metrics_.IntGid1("7FFFFFFFFFFFFFFF");  // 16 digits
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 0x7FFFFFFFFFFFFFF);   // last digit removed
  val = metrics_.IntGid1("FFFFFFFFFFFFFFFF");  // 16 digits
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 0xFFFFFFFFFFFFFFF);  // last digit removed
  EXPECT_TRUE(val.has_value());
}

TEST_F(MetricsTest, Logging) {
  NiceScopedMockLog log;
  const int kVerboseLevel5 = -5;
  ScopeLogger::GetInstance()->EnableScopesByName("+metrics");
  ScopeLogger::GetInstance()->set_verbose_level(-kVerboseLevel5);

  const std::string kEnumName("fake-enum");
  const int kEnumValue = 1;
  const int kEnumMax = 12;
  EXPECT_CALL(log,
              Log(kVerboseLevel5, _, "Sending enum fake-enum with value 1."));
  EXPECT_CALL(library_, SendEnumToUMA(kEnumName, kEnumValue, kEnumMax));
  metrics_.SendEnumToUMA(kEnumName, kEnumValue, kEnumMax);

  const std::string kMetricName("fake-metric");
  const int kMetricValue = 2;
  const int kHistogramMin = 0;
  const int kHistogramMax = 100;
  const int kHistogramBuckets = 10;
  EXPECT_CALL(
      log, Log(kVerboseLevel5, _, "Sending metric fake-metric with value 2."));
  EXPECT_CALL(library_, SendToUMA(kMetricName, kMetricValue, kHistogramMin,
                                  kHistogramMax, kHistogramBuckets));
  metrics_.SendToUMA(kMetricName, kMetricValue, kHistogramMin, kHistogramMax,
                     kHistogramBuckets);

  ScopeLogger::GetInstance()->EnableScopesByName("-metrics");
  ScopeLogger::GetInstance()->set_verbose_level(0);
}

TEST_F(MetricsTest, NotifySuspendActionsCompleted_Success) {
  base::TimeDelta non_zero_time_delta = base::Milliseconds(1);
  chromeos_metrics::TimerMock* mock_time_suspend_actions_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_suspend_actions_timer(mock_time_suspend_actions_timer);
  EXPECT_CALL(*mock_time_suspend_actions_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(non_zero_time_delta), Return(true)));
  EXPECT_CALL(*mock_time_suspend_actions_timer, HasStarted())
      .WillOnce(Return(true));
  EXPECT_CALL(library_,
              SendToUMA(Eq(Metrics::kMetricSuspendActionTimeTaken.n.name),
                        non_zero_time_delta.InMilliseconds(),
                        Metrics::kMetricSuspendActionTimeTaken.min,
                        Metrics::kMetricSuspendActionTimeTaken.max,
                        Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifySuspendActionsCompleted(true);
}

TEST_F(MetricsTest, NotifySuspendActionsCompleted_Failure) {
  base::TimeDelta non_zero_time_delta = base::Milliseconds(1);
  chromeos_metrics::TimerMock* mock_time_suspend_actions_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_suspend_actions_timer(mock_time_suspend_actions_timer);
  EXPECT_CALL(*mock_time_suspend_actions_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(non_zero_time_delta), Return(true)));
  EXPECT_CALL(*mock_time_suspend_actions_timer, HasStarted())
      .WillOnce(Return(true));
  EXPECT_CALL(library_,
              SendToUMA(Eq(Metrics::kMetricSuspendActionTimeTaken.n.name),
                        non_zero_time_delta.InMilliseconds(),
                        Metrics::kMetricSuspendActionTimeTaken.min,
                        Metrics::kMetricSuspendActionTimeTaken.max,
                        Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifySuspendActionsCompleted(false);
}

TEST_F(MetricsTest, NotifySuspendActionsStarted) {
  metrics_.time_suspend_actions_timer->Stop();
  metrics_.NotifySuspendActionsStarted();
  EXPECT_TRUE(metrics_.time_suspend_actions_timer->HasStarted());
}

TEST_F(MetricsTest, NotifyConnectionDiagnosticsIssue_Success) {
  const std::string& issue = ConnectionDiagnostics::kIssueIPCollision;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(Eq(Metrics::kMetricConnectionDiagnosticsIssue.n.name),
                    Metrics::kConnectionDiagnosticsIssueIPCollision,
                    Metrics::kConnectionDiagnosticsIssueMax));
  metrics_.NotifyConnectionDiagnosticsIssue(issue);
}

TEST_F(MetricsTest, NotifyConnectionDiagnosticsIssue_Failure) {
  const std::string& invalid_issue = "Invalid issue string.";
  EXPECT_CALL(library_, SendEnumToUMA(_, _, _)).Times(0);
  metrics_.NotifyConnectionDiagnosticsIssue(invalid_issue);
}

TEST_F(MetricsTest, NotifyAp80211kSupport) {
  bool neighbor_list_supported = false;
  EXPECT_CALL(library_, SendBoolToUMA(Metrics::kMetricAp80211kSupport,
                                      neighbor_list_supported));
  metrics_.NotifyAp80211kSupport(neighbor_list_supported);

  neighbor_list_supported = true;
  EXPECT_CALL(library_, SendBoolToUMA(Metrics::kMetricAp80211kSupport,
                                      neighbor_list_supported));
  metrics_.NotifyAp80211kSupport(neighbor_list_supported);
}

TEST_F(MetricsTest, NotifyAp80211rSupport) {
  bool ota_ft_supported = false;
  bool otds_ft_supported = false;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(Eq(Metrics::kMetricAp80211rSupport.n.name),
                    Metrics::kWiFiAp80211rNone, Metrics::kWiFiAp80211rMax));
  metrics_.NotifyAp80211rSupport(ota_ft_supported, otds_ft_supported);

  ota_ft_supported = true;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(Eq(Metrics::kMetricAp80211rSupport.n.name),
                    Metrics::kWiFiAp80211rOTA, Metrics::kWiFiAp80211rMax));
  metrics_.NotifyAp80211rSupport(ota_ft_supported, otds_ft_supported);

  otds_ft_supported = true;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(Eq(Metrics::kMetricAp80211rSupport.n.name),
                    Metrics::kWiFiAp80211rOTDS, Metrics::kWiFiAp80211rMax));
  metrics_.NotifyAp80211rSupport(ota_ft_supported, otds_ft_supported);
}

TEST_F(MetricsTest, NotifyAp80211vDMSSupport) {
  bool dms_supported = false;
  EXPECT_CALL(library_,
              SendBoolToUMA(Metrics::kMetricAp80211vDMSSupport, dms_supported));
  metrics_.NotifyAp80211vDMSSupport(dms_supported);

  dms_supported = true;
  EXPECT_CALL(library_,
              SendBoolToUMA(Metrics::kMetricAp80211vDMSSupport, dms_supported));
  metrics_.NotifyAp80211vDMSSupport(dms_supported);
}

TEST_F(MetricsTest, NotifyAp80211vBSSMaxIdlePeriodSupport) {
  bool bss_max_idle_period_supported = false;
  EXPECT_CALL(library_,
              SendBoolToUMA(Metrics::kMetricAp80211vBSSMaxIdlePeriodSupport,
                            bss_max_idle_period_supported));
  metrics_.NotifyAp80211vBSSMaxIdlePeriodSupport(bss_max_idle_period_supported);

  bss_max_idle_period_supported = true;
  EXPECT_CALL(library_,
              SendBoolToUMA(Metrics::kMetricAp80211vBSSMaxIdlePeriodSupport,
                            bss_max_idle_period_supported));
  metrics_.NotifyAp80211vBSSMaxIdlePeriodSupport(bss_max_idle_period_supported);
}

TEST_F(MetricsTest, NotifyAp80211vBSSTransitionSupport) {
  bool bss_transition_supported = false;
  EXPECT_CALL(library_,
              SendBoolToUMA(Metrics::kMetricAp80211vBSSTransitionSupport,
                            bss_transition_supported));
  metrics_.NotifyAp80211vBSSTransitionSupport(bss_transition_supported);

  bss_transition_supported = true;
  EXPECT_CALL(library_,
              SendBoolToUMA(Metrics::kMetricAp80211vBSSTransitionSupport,
                            bss_transition_supported));
  metrics_.NotifyAp80211vBSSTransitionSupport(bss_transition_supported);
}

TEST_F(MetricsTest, NotifyCiscoAdaptiveFTSupportFalse) {
  bool adaptive_ft_supported = false;
  EXPECT_CALL(library_, SendBoolToUMA(Metrics::kMetricCiscoAdaptiveFTSupport,
                                      adaptive_ft_supported));
  metrics_.NotifyCiscoAdaptiveFTSupport(adaptive_ft_supported);
}

TEST_F(MetricsTest, NotifyCiscoAdaptiveFTSupportTrue) {
  bool adaptive_ft_supported = true;
  EXPECT_CALL(library_, SendBoolToUMA(Metrics::kMetricCiscoAdaptiveFTSupport,
                                      adaptive_ft_supported));
  metrics_.NotifyCiscoAdaptiveFTSupport(adaptive_ft_supported);
}

TEST_F(MetricsTest, NotifyApChannelSwitch) {
  EXPECT_CALL(library_,
              SendEnumToUMA(Eq(Metrics::kMetricApChannelSwitch.n.name),
                            Metrics::kWiFiApChannelSwitch24To24,
                            Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(2417, 2472);

  EXPECT_CALL(library_,
              SendEnumToUMA(Eq(Metrics::kMetricApChannelSwitch.n.name),
                            Metrics::kWiFiApChannelSwitch24To5,
                            Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(2462, 5805);

  EXPECT_CALL(library_,
              SendEnumToUMA(Eq(Metrics::kMetricApChannelSwitch.n.name),
                            Metrics::kWiFiApChannelSwitch5To24,
                            Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(5210, 2422);

  EXPECT_CALL(library_,
              SendEnumToUMA(Eq(Metrics::kMetricApChannelSwitch.n.name),
                            Metrics::kWiFiApChannelSwitch5To5,
                            Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(5500, 5320);

  EXPECT_CALL(library_,
              SendEnumToUMA(Eq(Metrics::kMetricApChannelSwitch.n.name),
                            Metrics::kWiFiApChannelSwitchUndef,
                            Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(3000, 3000);
}

TEST_F(MetricsTest, NotifyWiFiBadPassphraseNonUserInitiatedNeverConnected) {
  bool ever_connected = false;
  bool user_initiate = false;
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.WiFi.BadPassphraseServiceType", 0,
                            Metrics::kBadPassphraseServiceTypeMax))
      .Times(1);
  metrics_.NotifyWiFiBadPassphrase(ever_connected, user_initiate);
}

TEST_F(MetricsTest, NotifyWiFiBadPassphraseUserInitiatedNeverConnected) {
  bool ever_connected = false;
  bool user_initiate = true;
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.WiFi.BadPassphraseServiceType", 2,
                            Metrics::kBadPassphraseServiceTypeMax))
      .Times(1);
  metrics_.NotifyWiFiBadPassphrase(ever_connected, user_initiate);
}

TEST_F(MetricsTest, NotifyWiFiBadPassphraseNonUserInitiatedConnectedBefore) {
  bool ever_connected = true;
  bool user_initiate = false;
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.WiFi.BadPassphraseServiceType", 1,
                            Metrics::kBadPassphraseServiceTypeMax))
      .Times(1);
  metrics_.NotifyWiFiBadPassphrase(ever_connected, user_initiate);
}

TEST_F(MetricsTest, NotifyWiFiBadPassphraseUserInitiatedConnectedBefore) {
  bool ever_connected = true;
  bool user_initiate = true;
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.WiFi.BadPassphraseServiceType", 3,
                            Metrics::kBadPassphraseServiceTypeMax))
      .Times(1);
  metrics_.NotifyWiFiBadPassphrase(ever_connected, user_initiate);
}

TEST_F(MetricsTest, NotifyWiFiAdapterStateDisabledNoAllowlistUMA) {
  EXPECT_CALL(library_, SendEnumToUMA(_, _, _)).Times(AnyNumber());
  // Verify that we do not emit any "AdapterAllowlisted" UMA event if the
  // adapter is disabled.
  const std::string name = "Network.Shill.WiFi.AdapterAllowlisted";
  EXPECT_CALL(library_, SendEnumToUMA(name, _, _)).Times(0);
  metrics_.NotifyWiFiAdapterStateChanged(false /* enabled */,
                                         Metrics::WiFiAdapterInfo());
}

TEST_F(MetricsTest, NotifyWiFiAdapterStateEnabledEmitsAllowlistUMA) {
  EXPECT_CALL(library_, SendEnumToUMA(_, _, _)).Times(AnyNumber());
  // Verify that we emit 1 "AdapterAllowlisted" UMA event if the adapter is
  // enabled.
  const std::string name = "Network.Shill.WiFi.AdapterAllowlisted";
  EXPECT_CALL(library_, SendEnumToUMA(name, _, _)).Times(1);
  metrics_.NotifyWiFiAdapterStateChanged(true /* enabled */,
                                         Metrics::WiFiAdapterInfo());
}

TEST_F(MetricsTest, NotifyWiFiAdapterStateChangedEmitsChipsetInfoEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiChipsetInfo event.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::wi_fi_chipset::
                                  WiFiChipsetInfo::kEventNameHash)))
      .Times(1);

  metrics_.NotifyWiFiAdapterStateChanged(bool(), Metrics::WiFiAdapterInfo());
}

TEST_F(MetricsTest, NotifyWiFiAdapterStateChangedEmitsAdapterInfoEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiAdapterStateChanged event.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::wi_fi::
                                  WiFiAdapterStateChanged::kEventNameHash)))
      .Times(1);

  metrics_.NotifyWiFiAdapterStateChanged(bool(), Metrics::WiFiAdapterInfo());
}

TEST_F(MetricsTest, NotifyWiFiConnectionAttemptEmitsAPInfoEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiAPInfo event.
  EXPECT_CALL(
      *recorder_,
      Record(testing::Property(
          &metrics::structured::EventBase::name_hash,
          metrics::structured::events::wi_fi_ap::WiFiAPInfo::kEventNameHash)))
      .Times(1);
  constexpr uint64_t tag = 0x123456789;
  metrics_.NotifyWiFiConnectionAttempt(Metrics::WiFiConnectionAttemptInfo(),
                                       tag);
}

TEST_F(MetricsTest, NotifyWiFiConnectionAttemptEmitsConnectionAttemptEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiConnectionAttempt event.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::wi_fi::
                                  WiFiConnectionAttempt::kEventNameHash)))
      .Times(1);
  constexpr uint64_t tag = 0x123456789;
  metrics_.NotifyWiFiConnectionAttempt(Metrics::WiFiConnectionAttemptInfo(),
                                       tag);
}

TEST_F(MetricsTest, NotifyWiFiConnectionAttemptResultEmitsAttemptResultEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiConnectionAttemptResult event.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::wi_fi::
                                  WiFiConnectionAttemptResult::kEventNameHash)))
      .Times(1);
  constexpr uint64_t tag = 0x123456789;
  metrics_.NotifyWiFiConnectionAttemptResult(
      Metrics::kNetworkServiceErrorBadPassphrase, tag);
}

TEST_F(MetricsTest, NotifyWiFiConnectionDisconnectionEmitsConnectionEndEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiConnectionEnd event.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::wi_fi::
                                  WiFiConnectionEnd::kEventNameHash)))
      .Times(1);
  constexpr uint64_t tag = 0x123456789;
  metrics_.NotifyWiFiDisconnection(
      Metrics::kWiFiDisconnectionTypeUnexpectedAPDisconnect,
      IEEE_80211::kReasonCodeTooManySTAs, tag);
}

TEST_F(MetricsTest, NotifyWiFiLinkQualityTriggerEmitsTriggerEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiLinkQualityTrigger event.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::wi_fi::
                                  WiFiLinkQualityTrigger::kEventNameHash)))
      .Times(1);
  constexpr uint64_t tag = 0x123456789;
  metrics_.NotifyWiFiLinkQualityTrigger(
      Metrics::kWiFiLinkQualityTriggerCQMBeaconLoss, tag);
}

TEST_F(MetricsTest, NotifyWiFiLinkQualityReportEmitsReportEvent) {
  EXPECT_CALL(*recorder_, Record(_)).Times(AnyNumber());
  // Verify that we emit 1 WiFiLinkQualityReport event.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::wi_fi::
                                  WiFiLinkQualityReport::kEventNameHash)))
      .Times(1);
  constexpr uint64_t tag = 0x123456789;
  metrics_.NotifyWiFiLinkQualityReport(Metrics::WiFiLinkQualityReport(), tag);
}

TEST_F(MetricsTest, WiFiRxTxStatsComparison) {
  Metrics::WiFiRxTxStats s1, s2;
  EXPECT_EQ(s1, s2);

  s1.packets = 5;
  s2.packets = 5;
  EXPECT_EQ(s1, s2);
  s2.packets = 7;
  EXPECT_NE(s1, s2);

  s1 = {};
  s2 = {};
  s1.bytes = 8;
  s2.bytes = 8;
  EXPECT_EQ(s1, s2);
  s2.bytes = 7;
  EXPECT_NE(s1, s2);

  s1 = {};
  s2 = {};
  s1.bitrate = 1000;
  s2.bitrate = 1000;
  EXPECT_EQ(s1, s2);
  s2.bitrate = 2000;
  EXPECT_NE(s1, s2);

  s1 = {};
  s2 = {};
  s1.mcs = 9;
  s2.mcs = 9;
  EXPECT_EQ(s1, s2);
  s2.mcs = 7;
  EXPECT_NE(s1, s2);

  s1 = {};
  s2 = {};
  s1.mode = Metrics::kWiFiLinkModeHE;
  s2.mode = Metrics::kWiFiLinkModeHE;
  EXPECT_EQ(s1, s2);
  s2.mode = Metrics::kWiFiLinkModeVHT;
  EXPECT_NE(s1, s2);

  s1 = {};
  s2 = {};
  s1.gi = Metrics::kWiFiGuardInterval_0_8;
  s2.gi = Metrics::kWiFiGuardInterval_0_8;
  EXPECT_EQ(s1, s2);
  s2.gi = Metrics::kWiFiGuardInterval_1_6;
  EXPECT_NE(s1, s2);

  s1 = {};
  s2 = {};
  s1.nss = 2;
  s2.nss = 2;
  EXPECT_EQ(s1, s2);
  s2.nss = 4;
  EXPECT_NE(s1, s2);

  s1 = {};
  s2 = {};
  s1.dcm = 1;
  s2.dcm = 1;
  EXPECT_EQ(s1, s2);
  s2.dcm = 0;
  EXPECT_NE(s1, s2);
}

TEST_F(MetricsTest, WiFiLinkQualityReportComparison) {
  Metrics::WiFiLinkQualityReport r1, r2;
  EXPECT_EQ(r1, r2);

  r1.tx_retries = 5;
  r2.tx_retries = 5;
  EXPECT_EQ(r1, r2);
  r2.tx_retries = 7;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.tx_failures = 2;
  r2.tx_failures = 2;
  EXPECT_EQ(r1, r2);
  r2.tx_failures = 3;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.rx_drops = 3;
  r2.rx_drops = 3;
  EXPECT_EQ(r1, r2);
  r2.rx_drops = 1;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.chain0_signal = -55;
  r2.chain0_signal = -55;
  EXPECT_EQ(r1, r2);
  r2.chain0_signal = -60;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.chain0_signal_avg = -51;
  r2.chain0_signal_avg = -51;
  EXPECT_EQ(r1, r2);
  r2.chain0_signal_avg = -63;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.chain1_signal = -55;
  r2.chain1_signal = -55;
  EXPECT_EQ(r1, r2);
  r2.chain1_signal = -60;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.chain1_signal_avg = -50;
  r2.chain1_signal_avg = -50;
  EXPECT_EQ(r1, r2);
  r2.chain1_signal_avg = -52;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.beacon_signal_avg = -53;
  r2.beacon_signal_avg = -53;
  EXPECT_EQ(r1, r2);
  r2.beacon_signal_avg = -54;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.beacons_received = 535;
  r2.beacons_received = 535;
  EXPECT_EQ(r1, r2);
  r2.beacons_received = 700;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.beacons_lost = 4;
  r2.beacons_lost = 4;
  EXPECT_EQ(r1, r2);
  r2.beacons_lost = 3;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.expected_throughput = 15000;
  r2.expected_throughput = 15000;
  EXPECT_EQ(r1, r2);
  r2.expected_throughput = 16000;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.width = Metrics::kWiFiChannelWidth80MHz;
  r2.width = Metrics::kWiFiChannelWidth80MHz;
  EXPECT_EQ(r1, r2);
  r2.width = Metrics::kWiFiChannelWidth40MHz;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.rx.bitrate = 20000;
  r2.rx.bitrate = 20000;
  EXPECT_EQ(r1, r2);
  r2.rx.bitrate = 17000;
  EXPECT_NE(r1, r2);

  r1 = {};
  r2 = {};
  r1.tx.bitrate = 25000;
  r2.tx.bitrate = 25000;
  EXPECT_EQ(r1, r2);
  r2.tx.bitrate = 18000;
  EXPECT_NE(r1, r2);

  r1 = {};
  EXPECT_EQ(r1.bt_enabled, false);
  r2 = {};
  r1.bt_enabled = true;
  r2.bt_enabled = true;
  EXPECT_EQ(r1, r2);
  r2.bt_enabled = false;
  EXPECT_NE(r1, r2);

  r1 = {};
  EXPECT_EQ(r1.bt_stack, Metrics::kBTStackUnknown);
  r2 = {};
  r1.bt_stack = Metrics::kBTStackFloss;
  r2.bt_stack = Metrics::kBTStackFloss;
  EXPECT_EQ(r1, r2);
  r2.bt_stack = Metrics::kBTStackBlueZ;
  EXPECT_NE(r1, r2);

  r1 = {};
  EXPECT_EQ(r1.bt_hfp, Metrics::kBTProfileConnectionStateInvalid);
  r2 = {};
  r1.bt_hfp = Metrics::kBTProfileConnectionStateConnected;
  r2.bt_hfp = Metrics::kBTProfileConnectionStateConnected;
  EXPECT_EQ(r1, r2);
  r2.bt_hfp = Metrics::kBTProfileConnectionStateDisconnecting;
  EXPECT_NE(r1, r2);

  r1 = {};
  EXPECT_EQ(r1.bt_a2dp, Metrics::kBTProfileConnectionStateInvalid);
  r2 = {};
  r1.bt_a2dp = Metrics::kBTProfileConnectionStateConnecting;
  r2.bt_a2dp = Metrics::kBTProfileConnectionStateConnecting;
  EXPECT_EQ(r1, r2);
  r2.bt_a2dp = Metrics::kBTProfileConnectionStateConnected;
  EXPECT_NE(r1, r2);

  r1 = {};
  EXPECT_EQ(r1.bt_active_scanning, false);
  r2 = {};
  r1.bt_active_scanning = true;
  r2.bt_active_scanning = true;
  EXPECT_EQ(r1, r2);
  r2.bt_active_scanning = false;
  EXPECT_NE(r1, r2);
}

TEST_F(MetricsTest, BTProfileConnectionStateIntegerValues) {
  // Integer values are interpreted by the server-side pipeline, ensure that
  // they are not changed over time.
  EXPECT_EQ(Metrics::kBTProfileConnectionStateInvalid, 0x7FFFFFFE);
  EXPECT_EQ(Metrics::kBTProfileConnectionStateDisconnected, 0);
  EXPECT_EQ(Metrics::kBTProfileConnectionStateDisconnecting, 1);
  EXPECT_EQ(Metrics::kBTProfileConnectionStateConnecting, 2);
  EXPECT_EQ(Metrics::kBTProfileConnectionStateConnected, 3);
  EXPECT_EQ(Metrics::kBTProfileConnectionStateActive, 4);
}

}  // namespace shill
