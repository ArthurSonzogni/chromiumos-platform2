// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/metrics.h"

#include <string>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>
#include <metrics/metrics_library_mock.h>
#include <metrics/timer_mock.h>

#include "shill/mock_control.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_service.h"
#include "shill/test_event_dispatcher.h"

#if !defined(DISABLE_WIFI)
#include "shill/mock_eap_credentials.h"
#include "shill/wifi/mock_wifi_service.h"
#endif  // DISABLE_WIFI

using testing::_;
using testing::DoAll;
using testing::Ge;
using testing::Mock;
using testing::Return;
using testing::SetArgPointee;
using testing::Test;

namespace shill {

class CumulativeMetricsMock : public chromeos_metrics::CumulativeMetrics {
 public:
  CumulativeMetricsMock(const base::FilePath& backing_dir,
                        const std::vector<std::string>& names,
                        base::TimeDelta update_period,
                        Callback update_callback,
                        base::TimeDelta accumulation_period,
                        Callback cycle_end_callback)
      : CumulativeMetrics(backing_dir,
                          names,
                          update_period,
                          update_callback,
                          accumulation_period,
                          cycle_end_callback) {}
  void SetActiveTimes(std::vector<int> times);
  virtual base::TimeDelta ActiveTimeSinceLastUpdate() const;

 private:
  std::vector<int> active_times_;
  mutable int active_times_index_;
};

void CumulativeMetricsMock::SetActiveTimes(std::vector<int> times) {
  active_times_ = times;
  active_times_index_ = 0;
}

base::TimeDelta CumulativeMetricsMock::ActiveTimeSinceLastUpdate() const {
  auto time = active_times_.at(active_times_index_);
  active_times_index_ += 1;
  return base::TimeDelta::FromSeconds(time);
}

class MetricsTest : public Test {
 public:
  MetricsTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
#if !defined(DISABLE_WIFI)
        open_wifi_service_(new MockWiFiService(&manager_,
                                               manager_.wifi_provider(),
                                               ssid_,
                                               kModeManaged,
                                               kSecurityNone,
                                               false)),
        wep_wifi_service_(new MockWiFiService(&manager_,
                                              manager_.wifi_provider(),
                                              ssid_,
                                              kModeManaged,
                                              kSecurityWep,
                                              false)),
        eap_wifi_service_(new MockWiFiService(&manager_,
                                              manager_.wifi_provider(),
                                              ssid_,
                                              kModeManaged,
                                              kSecurity8021x,
                                              false)),
        eap_(new MockEapCredentials()),
#endif  // DISABLE_WIFI
        service_(new MockService(&manager_)) {
  }

  ~MetricsTest() override = default;

  void SetUp() override {
    metrics_.set_library(&library_);
#if !defined(DISABLE_WIFI)
    eap_wifi_service_->eap_.reset(eap_);  // Passes ownership.
#endif                                    // DISABLE_WIFI
    metrics_.collect_bootstats_ = false;
  }

 protected:
  void ExpectCommonPostReady(Metrics::WiFiChannel channel,
                             Metrics::WiFiNetworkPhyMode mode,
                             Metrics::WiFiSecurity security,
                             int signal_strength) {
    EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Wifi.Channel", channel,
                                        Metrics::kMetricNetworkChannelMax));
    EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Wifi.PhyMode", mode,
                                        Metrics::kWiFiNetworkPhyModeMax));
    EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.Wifi.Security", security,
                                        Metrics::kWiFiSecurityMax));
    EXPECT_CALL(library_,
                SendToUMA("Network.Shill.Wifi.SignalStrength", signal_strength,
                          Metrics::kMetricNetworkSignalStrengthMin,
                          Metrics::kMetricNetworkSignalStrengthMax,
                          Metrics::kMetricNetworkSignalStrengthNumBuckets));
  }

  MockControl control_interface_;
  EventDispatcherForTest dispatcher_;
  MockManager manager_;
  Metrics metrics_;  // This must be destroyed after all |service_|s.
  MetricsLibraryMock library_;
#if !defined(DISABLE_WIFI)
  const std::vector<uint8_t> ssid_;
  scoped_refptr<MockWiFiService> open_wifi_service_;
  scoped_refptr<MockWiFiService> wep_wifi_service_;
  scoped_refptr<MockWiFiService> eap_wifi_service_;
  MockEapCredentials* eap_;  // Owned by |eap_wifi_service_|.
#endif                       // DISABLE_WIFI
  scoped_refptr<MockService> service_;
};

TEST_F(MetricsTest, TimeToConfig) {
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Unknown.TimeToConfig", Ge(0),
                                  Metrics::kTimerHistogramMillisecondsMin,
                                  Metrics::kTimerHistogramMillisecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifyServiceStateChanged(*service_, Service::kStateConfiguring);
  metrics_.NotifyServiceStateChanged(*service_, Service::kStateConnected);
}

TEST_F(MetricsTest, TimeToPortal) {
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Unknown.TimeToPortal", Ge(0),
                                  Metrics::kTimerHistogramMillisecondsMin,
                                  Metrics::kTimerHistogramMillisecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifyServiceStateChanged(*service_, Service::kStateConnected);
  metrics_.NotifyServiceStateChanged(*service_, Service::kStateNoConnectivity);
}

TEST_F(MetricsTest, TimeToOnline) {
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Unknown.TimeToOnline", Ge(0),
                                  Metrics::kTimerHistogramMillisecondsMin,
                                  Metrics::kTimerHistogramMillisecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifyServiceStateChanged(*service_, Service::kStateConnected);
  metrics_.NotifyServiceStateChanged(*service_, Service::kStateOnline);
}

TEST_F(MetricsTest, ServiceFailure) {
  EXPECT_CALL(*service_, failure())
      .WillRepeatedly(Return(Service::kFailureBadPassphrase));
  EXPECT_CALL(*service_, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.Wifi.ServiceErrors",
                            Metrics::kNetworkServiceErrorBadPassphrase,
                            Metrics::kNetworkServiceErrorMax));
  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricNetworkServiceErrors,
                            Metrics::kNetworkServiceErrorBadPassphrase,
                            Metrics::kNetworkServiceErrorMax));
  metrics_.NotifyServiceStateChanged(*service_, Service::kStateFailure);
}

#if !defined(DISABLE_WIFI)
TEST_F(MetricsTest, WiFiServiceTimeToJoin) {
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Wifi.TimeToJoin", Ge(0),
                                  Metrics::kTimerHistogramMillisecondsMin,
                                  Metrics::kTimerHistogramMillisecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifyServiceStateChanged(*open_wifi_service_,
                                     Service::kStateAssociating);
  metrics_.NotifyServiceStateChanged(*open_wifi_service_,
                                     Service::kStateConfiguring);
}

TEST_F(MetricsTest, WiFiServicePostReady) {
  base::TimeDelta non_zero_time_delta = base::TimeDelta::FromMilliseconds(1);
  chromeos_metrics::TimerMock* mock_time_resume_to_ready_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_resume_to_ready_timer(mock_time_resume_to_ready_timer);

  const int kStrength = -42;
  ExpectCommonPostReady(Metrics::kWiFiChannel2412,
                        Metrics::kWiFiNetworkPhyMode11a,
                        Metrics::kWiFiSecurityWep, -kStrength);
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Wifi.TimeResumeToReady", _, _, _, _))
      .Times(0);
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.Wifi.EapOuterProtocol", _, _))
      .Times(0);
  EXPECT_CALL(library_,
              SendEnumToUMA("Network.Shill.Wifi.EapInnerProtocol", _, _))
      .Times(0);
  wep_wifi_service_->frequency_ = 2412;
  wep_wifi_service_->physical_mode_ = Metrics::kWiFiNetworkPhyMode11a;
  wep_wifi_service_->raw_signal_strength_ = kStrength;
  metrics_.NotifyServiceStateChanged(*wep_wifi_service_,
                                     Service::kStateConnected);
  Mock::VerifyAndClearExpectations(&library_);

  // Simulate a system suspend, resume and an AP reconnect.
  ExpectCommonPostReady(Metrics::kWiFiChannel2412,
                        Metrics::kWiFiNetworkPhyMode11a,
                        Metrics::kWiFiSecurityWep, -kStrength);
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Wifi.TimeResumeToReady", Ge(0),
                                  Metrics::kTimerHistogramMillisecondsMin,
                                  Metrics::kTimerHistogramMillisecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  EXPECT_CALL(*mock_time_resume_to_ready_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(non_zero_time_delta), Return(true)));
  metrics_.NotifySuspendDone();
  metrics_.NotifyServiceStateChanged(*wep_wifi_service_,
                                     Service::kStateConnected);
  Mock::VerifyAndClearExpectations(&library_);
  Mock::VerifyAndClearExpectations(mock_time_resume_to_ready_timer);

  // Make sure subsequent connects do not count towards TimeResumeToReady.
  ExpectCommonPostReady(Metrics::kWiFiChannel2412,
                        Metrics::kWiFiNetworkPhyMode11a,
                        Metrics::kWiFiSecurityWep, -kStrength);
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.Wifi.TimeResumeToReady", _, _, _, _))
      .Times(0);
  metrics_.NotifyServiceStateChanged(*wep_wifi_service_,
                                     Service::kStateConnected);
}

TEST_F(MetricsTest, WiFiServicePostReadyEAP) {
  const int kStrength = -42;
  ExpectCommonPostReady(Metrics::kWiFiChannel2412,
                        Metrics::kWiFiNetworkPhyMode11a,
                        Metrics::kWiFiSecurity8021x, -kStrength);
  eap_wifi_service_->frequency_ = 2412;
  eap_wifi_service_->physical_mode_ = Metrics::kWiFiNetworkPhyMode11a;
  eap_wifi_service_->raw_signal_strength_ = kStrength;
  EXPECT_CALL(
      *eap_, OutputConnectionMetrics(&metrics_, Technology(Technology::kWifi)));
  metrics_.NotifyServiceStateChanged(*eap_wifi_service_,
                                     Service::kStateConnected);
}
#endif  // DISABLE_WIFI

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

TEST_F(MetricsTest, TimeOnlineTimeToDrop) {
  chromeos_metrics::TimerMock* mock_time_online_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_online_timer(mock_time_online_timer);
  chromeos_metrics::TimerMock* mock_time_to_drop_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_to_drop_timer(mock_time_to_drop_timer);
  scoped_refptr<MockService> wifi_service = new MockService(&manager_);
  EXPECT_CALL(*service_, technology()).WillOnce(Return(Technology::kEthernet));
  EXPECT_CALL(*wifi_service, technology()).WillOnce(Return(Technology::kWifi));
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Ethernet.TimeOnline", Ge(0),
                                  Metrics::kMetricTimeOnlineSecondsMin,
                                  Metrics::kMetricTimeOnlineSecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  EXPECT_CALL(library_, SendToUMA(Metrics::kMetricTimeToDropSeconds, Ge(0),
                                  Metrics::kMetricTimeToDropSecondsMin,
                                  Metrics::kMetricTimeToDropSecondsMax,
                                  Metrics::kTimerHistogramNumBuckets))
      .Times(0);
  EXPECT_CALL(*mock_time_online_timer, Start()).Times(2);
  EXPECT_CALL(*mock_time_to_drop_timer, Start());
  metrics_.OnDefaultLogicalServiceChanged(service_);
  metrics_.OnDefaultLogicalServiceChanged(wifi_service);

  EXPECT_CALL(*mock_time_online_timer, Start());
  EXPECT_CALL(*mock_time_to_drop_timer, Start()).Times(0);
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Wifi.TimeOnline", Ge(0),
                                  Metrics::kMetricTimeOnlineSecondsMin,
                                  Metrics::kMetricTimeOnlineSecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  EXPECT_CALL(library_, SendToUMA(Metrics::kMetricTimeToDropSeconds, Ge(0),
                                  Metrics::kMetricTimeToDropSecondsMin,
                                  Metrics::kMetricTimeToDropSecondsMax,
                                  Metrics::kTimerHistogramNumBuckets));
  metrics_.OnDefaultLogicalServiceChanged(nullptr);
}

TEST_F(MetricsTest, Disconnect) {
  EXPECT_CALL(*service_, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*service_, explicitly_disconnected()).WillOnce(Return(false));
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Wifi.Disconnect", false,
                                  Metrics::kMetricDisconnectMin,
                                  Metrics::kMetricDisconnectMax,
                                  Metrics::kMetricDisconnectNumBuckets));
  metrics_.NotifyServiceDisconnect(*service_);

  EXPECT_CALL(*service_, explicitly_disconnected()).WillOnce(Return(true));
  EXPECT_CALL(library_, SendToUMA("Network.Shill.Wifi.Disconnect", true,
                                  Metrics::kMetricDisconnectMin,
                                  Metrics::kMetricDisconnectMax,
                                  Metrics::kMetricDisconnectNumBuckets));
  metrics_.NotifyServiceDisconnect(*service_);
}

TEST_F(MetricsTest, PortalDetectionResultToEnum) {
  PortalDetector::Result result;

  result.http_phase = PortalDetector::Phase::kDNS;
  result.http_status = PortalDetector::Status::kFailure;
  EXPECT_EQ(Metrics::kPortalResultDNSFailure,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kDNS;
  result.http_status = PortalDetector::Status::kTimeout;
  EXPECT_EQ(Metrics::kPortalResultDNSTimeout,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kConnection;
  result.http_status = PortalDetector::Status::kFailure;
  EXPECT_EQ(Metrics::kPortalResultConnectionFailure,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kConnection;
  result.http_status = PortalDetector::Status::kTimeout;
  EXPECT_EQ(Metrics::kPortalResultConnectionTimeout,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kHTTP;
  result.http_status = PortalDetector::Status::kFailure;
  EXPECT_EQ(Metrics::kPortalResultHTTPFailure,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kHTTP;
  result.http_status = PortalDetector::Status::kTimeout;
  EXPECT_EQ(Metrics::kPortalResultHTTPTimeout,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kContent;
  result.http_status = PortalDetector::Status::kSuccess;
  EXPECT_EQ(Metrics::kPortalResultSuccess,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kContent;
  result.http_status = PortalDetector::Status::kFailure;
  EXPECT_EQ(Metrics::kPortalResultContentFailure,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kContent;
  result.http_status = PortalDetector::Status::kTimeout;
  EXPECT_EQ(Metrics::kPortalResultContentTimeout,
            Metrics::PortalDetectionResultToEnum(result));

  result.http_phase = PortalDetector::Phase::kUnknown;
  result.http_status = PortalDetector::Status::kFailure;
  EXPECT_EQ(Metrics::kPortalResultUnknown,
            Metrics::PortalDetectionResultToEnum(result));
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
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kWifi);
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
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kWifi);
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
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kWifi);
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

TEST_F(MetricsTest, TimeToScanIgnore) {
  // Make sure TimeToScan is not sent if the elapsed time exceeds the max
  // value.  This simulates the case where the device is in an area with no
  // service.
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);
  base::TimeDelta large_time_delta = base::TimeDelta::FromMilliseconds(
      Metrics::kMetricTimeToScanMillisecondsMax + 1);
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

TEST_F(MetricsTest, Cellular3GPPRegistrationDelayedDropPosted) {
  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricCellular3GPPRegistrationDelayedDrop,
                            Metrics::kCellular3GPPRegistrationDelayedDropPosted,
                            Metrics::kCellular3GPPRegistrationDelayedDropMax));
  metrics_.Notify3GPPRegistrationDelayedDropPosted();
  Mock::VerifyAndClearExpectations(&library_);

  EXPECT_CALL(
      library_,
      SendEnumToUMA(Metrics::kMetricCellular3GPPRegistrationDelayedDrop,
                    Metrics::kCellular3GPPRegistrationDelayedDropCanceled,
                    Metrics::kCellular3GPPRegistrationDelayedDropMax));
  metrics_.Notify3GPPRegistrationDelayedDropCanceled();
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
  for (size_t index = 0; index < base::size(kUMATechnologyStrings); ++index) {
    EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricCellularDrop, index,
                                        Metrics::kCellularDropTechnologyMax));
    EXPECT_CALL(
        library_,
        SendToUMA(Metrics::kMetricCellularSignalStrengthBeforeDrop,
                  signal_strength,
                  Metrics::kMetricCellularSignalStrengthBeforeDropMin,
                  Metrics::kMetricCellularSignalStrengthBeforeDropMax,
                  Metrics::kMetricCellularSignalStrengthBeforeDropNumBuckets));
    metrics_.NotifyCellularDeviceDrop(kUMATechnologyStrings[index],
                                      signal_strength);
    Mock::VerifyAndClearExpectations(&library_);
  }
}

TEST_F(MetricsTest, NotifyCellularConnectionResult_Valid) {
  Error::Type error = Error::Type::kOperationFailed;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricCellularConnectResult,
          static_cast<int>(Metrics::CellularConnectResult::
                               kCellularConnectResultOperationFailed),
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultMax)));
  metrics_.NotifyCellularConnectionResult(error);
}

TEST_F(MetricsTest, NotifyCellularConnectionResult_Unknown) {
  Error::Type invalid_error = Error::Type::kNumErrors;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricCellularConnectResult,
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultUnknown),
          static_cast<int>(
              Metrics::CellularConnectResult::kCellularConnectResultMax)));
  metrics_.NotifyCellularConnectionResult(invalid_error);
}

TEST_F(MetricsTest, CellularOutOfCreditsReason) {
  EXPECT_CALL(
      library_,
      SendEnumToUMA(Metrics::kMetricCellularOutOfCreditsReason,
                    Metrics::kCellularOutOfCreditsReasonConnectDisconnectLoop,
                    Metrics::kCellularOutOfCreditsReasonMax));
  metrics_.NotifyCellularOutOfCredits(
      Metrics::kCellularOutOfCreditsReasonConnectDisconnectLoop);
  Mock::VerifyAndClearExpectations(&library_);

  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricCellularOutOfCreditsReason,
                            Metrics::kCellularOutOfCreditsReasonTxCongested,
                            Metrics::kCellularOutOfCreditsReasonMax));
  metrics_.NotifyCellularOutOfCredits(
      Metrics::kCellularOutOfCreditsReasonTxCongested);
  Mock::VerifyAndClearExpectations(&library_);

  EXPECT_CALL(
      library_,
      SendEnumToUMA(Metrics::kMetricCellularOutOfCreditsReason,
                    Metrics::kCellularOutOfCreditsReasonElongatedTimeWait,
                    Metrics::kCellularOutOfCreditsReasonMax));
  metrics_.NotifyCellularOutOfCredits(
      Metrics::kCellularOutOfCreditsReasonElongatedTimeWait);
}

TEST_F(MetricsTest, CorruptedProfile) {
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricCorruptedProfile,
                                      Metrics::kCorruptedProfile,
                                      Metrics::kCorruptedProfileMax));
  metrics_.NotifyCorruptedProfile();
}

TEST_F(MetricsTest, Logging) {
  NiceScopedMockLog log;
  const int kVerboseLevel5 = -5;
  ScopeLogger::GetInstance()->EnableScopesByName("+metrics");
  ScopeLogger::GetInstance()->set_verbose_level(-kVerboseLevel5);

  const std::string kEnumName("fake-enum");
  const int kEnumValue = 1;
  const int kEnumMax = 12;
  EXPECT_CALL(log, Log(kVerboseLevel5, _,
                       "(metrics) Sending enum fake-enum with value 1."));
  EXPECT_CALL(library_, SendEnumToUMA(kEnumName, kEnumValue, kEnumMax));
  metrics_.SendEnumToUMA(kEnumName, kEnumValue, kEnumMax);

  const std::string kMetricName("fake-metric");
  const int kMetricValue = 2;
  const int kHistogramMin = 0;
  const int kHistogramMax = 100;
  const int kHistogramBuckets = 10;
  EXPECT_CALL(log, Log(kVerboseLevel5, _,
                       "(metrics) Sending metric fake-metric with value 2."));
  EXPECT_CALL(library_, SendToUMA(kMetricName, kMetricValue, kHistogramMin,
                                  kHistogramMax, kHistogramBuckets));
  metrics_.SendToUMA(kMetricName, kMetricValue, kHistogramMin, kHistogramMax,
                     kHistogramBuckets);

  ScopeLogger::GetInstance()->EnableScopesByName("-metrics");
  ScopeLogger::GetInstance()->set_verbose_level(0);
}

TEST_F(MetricsTest, NotifyUserInitiatedEvent) {
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricUserInitiatedEvents,
                                      Metrics::kUserInitiatedEventWifiScan,
                                      Metrics::kUserInitiatedEventMax));
  metrics_.NotifyUserInitiatedEvent(Metrics::kUserInitiatedEventWifiScan);
}

TEST_F(MetricsTest, NotifyWifiTxBitrate) {
  EXPECT_CALL(library_, SendToUMA(Metrics::kMetricWifiTxBitrate, 1,
                                  Metrics::kMetricWifiTxBitrateMin,
                                  Metrics::kMetricWifiTxBitrateMax,
                                  Metrics::kMetricWifiTxBitrateNumBuckets));
  metrics_.NotifyWifiTxBitrate(1);
}

TEST_F(MetricsTest, NotifyUserInitiatedConnectionResult) {
  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult,
                            Metrics::kUserInitiatedConnectionResultSuccess,
                            Metrics::kUserInitiatedConnectionResultMax));
  metrics_.NotifyUserInitiatedConnectionResult(
      Metrics::kMetricWifiUserInitiatedConnectionResult,
      Metrics::kUserInitiatedConnectionResultSuccess);
}

TEST_F(MetricsTest, NotifyDhcpClientStatus) {
  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.DHCPClientStatus",
                                      Metrics::kDhcpClientStatusReboot,
                                      Metrics::kDhcpClientStatusMax));
  metrics_.NotifyDhcpClientStatus(Metrics::kDhcpClientStatusReboot);
}

TEST_F(MetricsTest, DeregisterDevice) {
  const int kInterfaceIndex = 1;
  metrics_.RegisterDevice(kInterfaceIndex, Technology::kCellular);

  EXPECT_CALL(library_, SendEnumToUMA("Network.Shill.DeviceRemovedEvent",
                                      Metrics::kDeviceTechnologyTypeCellular,
                                      Metrics::kDeviceTechnologyTypeMax));
  metrics_.DeregisterDevice(kInterfaceIndex);
}

TEST_F(MetricsTest, NotifyWakeOnWiFiFeaturesEnabledState) {
  const Metrics::WakeOnWiFiFeaturesEnabledState state =
      Metrics::kWakeOnWiFiFeaturesEnabledStateNone;
  EXPECT_CALL(
      library_,
      SendEnumToUMA("Network.Shill.WiFi.WakeOnWiFiFeaturesEnabledState", state,
                    Metrics::kWakeOnWiFiFeaturesEnabledStateMax));
  metrics_.NotifyWakeOnWiFiFeaturesEnabledState(state);
}

TEST_F(MetricsTest, NotifyVerifyWakeOnWiFiSettingsResult) {
  const Metrics::VerifyWakeOnWiFiSettingsResult result =
      Metrics::kVerifyWakeOnWiFiSettingsResultSuccess;
  EXPECT_CALL(
      library_,
      SendEnumToUMA("Network.Shill.WiFi.VerifyWakeOnWiFiSettingsResult", result,
                    Metrics::kVerifyWakeOnWiFiSettingsResultMax));
  metrics_.NotifyVerifyWakeOnWiFiSettingsResult(result);
}

TEST_F(MetricsTest, NotifyConnectedToServiceAfterWake) {
  const Metrics::WiFiConnectionStatusAfterWake status =
      Metrics::kWiFiConnectionStatusAfterWakeWoWOnConnected;
  EXPECT_CALL(
      library_,
      SendEnumToUMA("Network.Shill.WiFi.WiFiConnectionStatusAfterWake", status,
                    Metrics::kWiFiConnectionStatusAfterWakeMax));
  metrics_.NotifyConnectedToServiceAfterWake(status);
}

TEST_F(MetricsTest, NotifySuspendDurationAfterWake) {
  const Metrics::WiFiConnectionStatusAfterWake status =
      Metrics::kWiFiConnectionStatusAfterWakeWoWOnConnected;
  int seconds_in_suspend = 1;
  EXPECT_CALL(library_,
              SendToUMA("Network.Shill.WiFi.SuspendDurationWoWOnConnected",
                        seconds_in_suspend, Metrics::kSuspendDurationMin,
                        Metrics::kSuspendDurationMax,
                        Metrics::kSuspendDurationNumBuckets));
  metrics_.NotifySuspendDurationAfterWake(status, seconds_in_suspend);
}

TEST_F(MetricsTest, NotifyWakeOnWiFiThrottled) {
  EXPECT_FALSE(metrics_.wake_on_wifi_throttled_);
  metrics_.NotifyWakeOnWiFiThrottled();
  EXPECT_TRUE(metrics_.wake_on_wifi_throttled_);
}

TEST_F(MetricsTest, NotifySuspendWithWakeOnWiFiEnabledDone) {
  metrics_.wake_on_wifi_throttled_ = true;
  EXPECT_CALL(library_,
              SendBoolToUMA("Network.Shill.WiFi.WakeOnWiFiThrottled", true));
  metrics_.NotifySuspendWithWakeOnWiFiEnabledDone();

  metrics_.wake_on_wifi_throttled_ = false;
  EXPECT_CALL(library_,
              SendBoolToUMA("Network.Shill.WiFi.WakeOnWiFiThrottled", false));
  metrics_.NotifySuspendWithWakeOnWiFiEnabledDone();
}

TEST_F(MetricsTest, NotifySuspendActionsCompleted_Success) {
  base::TimeDelta non_zero_time_delta = base::TimeDelta::FromMilliseconds(1);
  chromeos_metrics::TimerMock* mock_time_suspend_actions_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_suspend_actions_timer(mock_time_suspend_actions_timer);
  metrics_.wake_reason_received_ = true;
  EXPECT_CALL(*mock_time_suspend_actions_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(non_zero_time_delta), Return(true)));
  EXPECT_CALL(*mock_time_suspend_actions_timer, HasStarted())
      .WillOnce(Return(true));
  EXPECT_CALL(library_,
              SendToUMA(Metrics::kMetricSuspendActionTimeTaken,
                        non_zero_time_delta.InMilliseconds(),
                        Metrics::kMetricSuspendActionTimeTakenMillisecondsMin,
                        Metrics::kMetricSuspendActionTimeTakenMillisecondsMax,
                        Metrics::kTimerHistogramNumBuckets));
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricSuspendActionResult,
                                      Metrics::kSuspendActionResultSuccess,
                                      Metrics::kSuspendActionResultMax));
  metrics_.NotifySuspendActionsCompleted(true);
  EXPECT_FALSE(metrics_.wake_reason_received_);
}

TEST_F(MetricsTest, NotifySuspendActionsCompleted_Failure) {
  base::TimeDelta non_zero_time_delta = base::TimeDelta::FromMilliseconds(1);
  chromeos_metrics::TimerMock* mock_time_suspend_actions_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_suspend_actions_timer(mock_time_suspend_actions_timer);
  metrics_.wake_reason_received_ = true;
  EXPECT_CALL(*mock_time_suspend_actions_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(non_zero_time_delta), Return(true)));
  EXPECT_CALL(*mock_time_suspend_actions_timer, HasStarted())
      .WillOnce(Return(true));
  EXPECT_CALL(library_,
              SendToUMA(Metrics::kMetricSuspendActionTimeTaken,
                        non_zero_time_delta.InMilliseconds(),
                        Metrics::kMetricSuspendActionTimeTakenMillisecondsMin,
                        Metrics::kMetricSuspendActionTimeTakenMillisecondsMax,
                        Metrics::kTimerHistogramNumBuckets));
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricSuspendActionResult,
                                      Metrics::kSuspendActionResultFailure,
                                      Metrics::kSuspendActionResultMax));
  metrics_.NotifySuspendActionsCompleted(false);
  EXPECT_FALSE(metrics_.wake_reason_received_);
}

TEST_F(MetricsTest, NotifyDarkResumeActionsCompleted_Success) {
  metrics_.num_scan_results_expected_in_dark_resume_ = 0;
  base::TimeDelta non_zero_time_delta = base::TimeDelta::FromMilliseconds(1);
  chromeos_metrics::TimerMock* mock_time_dark_resume_actions_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_dark_resume_actions_timer(
      mock_time_dark_resume_actions_timer);
  metrics_.wake_reason_received_ = true;
  const int non_zero_num_retries = 3;
  metrics_.dark_resume_scan_retries_ = non_zero_num_retries;
  EXPECT_CALL(*mock_time_dark_resume_actions_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(non_zero_time_delta), Return(true)));
  EXPECT_CALL(*mock_time_dark_resume_actions_timer, HasStarted())
      .WillOnce(Return(true));
  EXPECT_CALL(
      library_,
      SendToUMA(Metrics::kMetricDarkResumeActionTimeTaken,
                non_zero_time_delta.InMilliseconds(),
                Metrics::kMetricDarkResumeActionTimeTakenMillisecondsMin,
                Metrics::kMetricDarkResumeActionTimeTakenMillisecondsMax,
                Metrics::kTimerHistogramNumBuckets));
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricDarkResumeActionResult,
                                      Metrics::kDarkResumeActionResultSuccess,
                                      Metrics::kDarkResumeActionResultMax));
  EXPECT_CALL(
      library_,
      SendEnumToUMA(Metrics::kMetricDarkResumeUnmatchedScanResultReceived,
                    Metrics::kDarkResumeUnmatchedScanResultsReceivedFalse,
                    Metrics::kDarkResumeUnmatchedScanResultsReceivedMax));
  EXPECT_CALL(library_, SendToUMA(Metrics::kMetricDarkResumeScanNumRetries,
                                  non_zero_num_retries,
                                  Metrics::kMetricDarkResumeScanNumRetriesMin,
                                  Metrics::kMetricDarkResumeScanNumRetriesMax,
                                  Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifyDarkResumeActionsCompleted(true);
  EXPECT_FALSE(metrics_.wake_reason_received_);
}

TEST_F(MetricsTest, NotifyDarkResumeActionsCompleted_Failure) {
  metrics_.num_scan_results_expected_in_dark_resume_ = 0;
  base::TimeDelta non_zero_time_delta = base::TimeDelta::FromMilliseconds(1);
  chromeos_metrics::TimerMock* mock_time_dark_resume_actions_timer =
      new chromeos_metrics::TimerMock;
  metrics_.set_time_dark_resume_actions_timer(
      mock_time_dark_resume_actions_timer);
  metrics_.wake_reason_received_ = true;
  const int non_zero_num_retries = 3;
  metrics_.dark_resume_scan_retries_ = non_zero_num_retries;
  EXPECT_CALL(*mock_time_dark_resume_actions_timer, GetElapsedTime(_))
      .WillOnce(DoAll(SetArgPointee<0>(non_zero_time_delta), Return(true)));
  EXPECT_CALL(*mock_time_dark_resume_actions_timer, HasStarted())
      .WillOnce(Return(true));
  EXPECT_CALL(
      library_,
      SendToUMA(Metrics::kMetricDarkResumeActionTimeTaken,
                non_zero_time_delta.InMilliseconds(),
                Metrics::kMetricDarkResumeActionTimeTakenMillisecondsMin,
                Metrics::kMetricDarkResumeActionTimeTakenMillisecondsMax,
                Metrics::kTimerHistogramNumBuckets));
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricDarkResumeActionResult,
                                      Metrics::kDarkResumeActionResultFailure,
                                      Metrics::kDarkResumeActionResultMax));
  EXPECT_CALL(
      library_,
      SendEnumToUMA(Metrics::kMetricDarkResumeUnmatchedScanResultReceived,
                    Metrics::kDarkResumeUnmatchedScanResultsReceivedFalse,
                    Metrics::kDarkResumeUnmatchedScanResultsReceivedMax));
  EXPECT_CALL(library_, SendToUMA(Metrics::kMetricDarkResumeScanNumRetries,
                                  non_zero_num_retries,
                                  Metrics::kMetricDarkResumeScanNumRetriesMin,
                                  Metrics::kMetricDarkResumeScanNumRetriesMax,
                                  Metrics::kTimerHistogramNumBuckets));
  metrics_.NotifyDarkResumeActionsCompleted(false);
  EXPECT_FALSE(metrics_.wake_reason_received_);
}

TEST_F(MetricsTest, NotifySuspendActionsStarted) {
  metrics_.time_suspend_actions_timer->Stop();
  metrics_.wake_on_wifi_throttled_ = true;
  metrics_.NotifySuspendActionsStarted();
  EXPECT_TRUE(metrics_.time_suspend_actions_timer->HasStarted());
  EXPECT_FALSE(metrics_.wake_on_wifi_throttled_);
}

TEST_F(MetricsTest, NotifyDarkResumeActionsStarted) {
  metrics_.time_dark_resume_actions_timer->Stop();
  metrics_.num_scan_results_expected_in_dark_resume_ = 2;
  metrics_.dark_resume_scan_retries_ = 3;
  metrics_.NotifyDarkResumeActionsStarted();
  EXPECT_TRUE(metrics_.time_dark_resume_actions_timer->HasStarted());
  EXPECT_EQ(0, metrics_.num_scan_results_expected_in_dark_resume_);
  EXPECT_EQ(0, metrics_.dark_resume_scan_retries_);
}

TEST_F(MetricsTest, NotifyDarkResumeInitiateScan) {
  metrics_.num_scan_results_expected_in_dark_resume_ = 0;
  metrics_.NotifyDarkResumeInitiateScan();
  EXPECT_EQ(1, metrics_.num_scan_results_expected_in_dark_resume_);
}

TEST_F(MetricsTest, NotifyDarkResumeScanResultsReceived) {
  metrics_.num_scan_results_expected_in_dark_resume_ = 1;
  metrics_.NotifyDarkResumeScanResultsReceived();
  EXPECT_EQ(0, metrics_.num_scan_results_expected_in_dark_resume_);
}

TEST_F(MetricsTest, NotifyDarkResumeScanRetry) {
  const int initial_num_retries = 2;
  metrics_.dark_resume_scan_retries_ = initial_num_retries;
  metrics_.NotifyDarkResumeScanRetry();
  EXPECT_EQ(initial_num_retries + 1, metrics_.dark_resume_scan_retries_);
}

TEST_F(MetricsTest, NotifyBeforeSuspendActions_InDarkResume) {
  const bool in_dark_resume = true;
  bool is_connected;
  metrics_.dark_resume_scan_retries_ = 1;

  is_connected = true;
  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricDarkResumeScanRetryResult,
                            Metrics::kDarkResumeScanRetryResultConnected,
                            Metrics::kDarkResumeScanRetryResultMax));
  metrics_.NotifyBeforeSuspendActions(is_connected, in_dark_resume);

  is_connected = false;
  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricDarkResumeScanRetryResult,
                            Metrics::kDarkResumeScanRetryResultNotConnected,
                            Metrics::kDarkResumeScanRetryResultMax));
  metrics_.NotifyBeforeSuspendActions(is_connected, in_dark_resume);
}

TEST_F(MetricsTest, NotifyBeforeSuspendActions_NotInDarkResume) {
  const bool in_dark_resume = false;
  bool is_connected;
  metrics_.dark_resume_scan_retries_ = 1;

  is_connected = true;
  EXPECT_CALL(library_, SendEnumToUMA(_, _, _)).Times(0);
  metrics_.NotifyBeforeSuspendActions(is_connected, in_dark_resume);

  is_connected = false;
  EXPECT_CALL(library_, SendEnumToUMA(_, _, _)).Times(0);
  metrics_.NotifyBeforeSuspendActions(is_connected, in_dark_resume);
}

TEST_F(MetricsTest, NotifyConnectionDiagnosticsIssue_Success) {
  const std::string& issue = ConnectionDiagnostics::kIssueIPCollision;
  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricConnectionDiagnosticsIssue,
                            Metrics::kConnectionDiagnosticsIssueIPCollision,
                            Metrics::kConnectionDiagnosticsIssueMax));
  metrics_.NotifyConnectionDiagnosticsIssue(issue);
}

TEST_F(MetricsTest, NotifyConnectionDiagnosticsIssue_Failure) {
  const std::string& invalid_issue = "Invalid issue string.";
  EXPECT_CALL(library_, SendEnumToUMA(_, _, _)).Times(0);
  metrics_.NotifyConnectionDiagnosticsIssue(invalid_issue);
}

TEST_F(MetricsTest, NotifyPortalDetectionMultiProbeResult) {
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricPortalDetectionMultiProbeResult,
          Metrics::kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPUnblocked,
          Metrics::kPortalDetectionMultiProbeResultMax));
  metrics_.NotifyPortalDetectionMultiProbeResult(result);

  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricPortalDetectionMultiProbeResult,
          Metrics::kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPRedirected,
          Metrics::kPortalDetectionMultiProbeResultMax));
  metrics_.NotifyPortalDetectionMultiProbeResult(result);

  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kFailure;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricPortalDetectionMultiProbeResult,
          Metrics::kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPBlocked,
          Metrics::kPortalDetectionMultiProbeResultMax));
  metrics_.NotifyPortalDetectionMultiProbeResult(result);

  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricPortalDetectionMultiProbeResult,
          Metrics::kPortalDetectionMultiProbeResultHTTPSBlockedHTTPBlocked,
          Metrics::kPortalDetectionMultiProbeResultMax));
  metrics_.NotifyPortalDetectionMultiProbeResult(result);

  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricPortalDetectionMultiProbeResult,
          Metrics::kPortalDetectionMultiProbeResultHTTPSBlockedHTTPRedirected,
          Metrics::kPortalDetectionMultiProbeResultMax));
  metrics_.NotifyPortalDetectionMultiProbeResult(result);

  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  EXPECT_CALL(
      library_,
      SendEnumToUMA(
          Metrics::kMetricPortalDetectionMultiProbeResult,
          Metrics::kPortalDetectionMultiProbeResultHTTPSBlockedHTTPUnblocked,
          Metrics::kPortalDetectionMultiProbeResultMax));
  metrics_.NotifyPortalDetectionMultiProbeResult(result);

  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kRedirect;
  EXPECT_CALL(library_,
              SendEnumToUMA(Metrics::kMetricPortalDetectionMultiProbeResult,
                            Metrics::kPortalDetectionMultiProbeResultUndefined,
                            Metrics::kPortalDetectionMultiProbeResultMax));
  metrics_.NotifyPortalDetectionMultiProbeResult(result);
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
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricAp80211rSupport,
                                      Metrics::kWiFiAp80211rNone,
                                      Metrics::kWiFiAp80211rMax));
  metrics_.NotifyAp80211rSupport(ota_ft_supported, otds_ft_supported);

  ota_ft_supported = true;
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricAp80211rSupport,
                                      Metrics::kWiFiAp80211rOTA,
                                      Metrics::kWiFiAp80211rMax));
  metrics_.NotifyAp80211rSupport(ota_ft_supported, otds_ft_supported);

  otds_ft_supported = true;
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricAp80211rSupport,
                                      Metrics::kWiFiAp80211rOTDS,
                                      Metrics::kWiFiAp80211rMax));
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

TEST_F(MetricsTest, NotifyApChannelSwitch) {
  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricApChannelSwitch,
                                      Metrics::kWiFiApChannelSwitch24To24,
                                      Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(2417, 2472);

  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricApChannelSwitch,
                                      Metrics::kWiFiApChannelSwitch24To5,
                                      Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(2462, 5805);

  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricApChannelSwitch,
                                      Metrics::kWiFiApChannelSwitch5To24,
                                      Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(5210, 2422);

  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricApChannelSwitch,
                                      Metrics::kWiFiApChannelSwitch5To5,
                                      Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(5500, 5320);

  EXPECT_CALL(library_, SendEnumToUMA(Metrics::kMetricApChannelSwitch,
                                      Metrics::kWiFiApChannelSwitchUndef,
                                      Metrics::kWiFiApChannelSwitchMax));
  metrics_.NotifyApChannelSwitch(3000, 3000);
}

TEST_F(MetricsTest, CumulativeCellWifiTest) {
  const int kCellIndex = 0;
  const int kWifiIndex = 1;

  // Set up a cumulative metric object, but don't wait for (real or fake) time
  // to trigger callbacks.  Instead call the Accumulate and Report functions
  // manually and verify that they accumulate quantities and send samples
  // correctly.
  const std::vector<std::string> cumulative_names = {
      "TimeOnlineAny",
      "TimeOnlineCell",
      "TimeOnlineWifi",
  };
  const std::vector<std::string> histogram_names = {
      "Histogram.TimeOnlineAny",      "Histogram.TimeOnlineCell",
      "Histogram.TimeOnlineWifi",     "Histogram.FractionOnlineCell",
      "Histogram.FractionOnlineWifi",
  };

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath backing_path(temp_dir.GetPath());
  auto cumulative_metrics = std::make_unique<CumulativeMetricsMock>(
      backing_path, cumulative_names, base::TimeDelta::FromSeconds(9999),
      base::Bind(&Metrics::AccumulateTimeOnTechnology, &metrics_,
                 cumulative_names),
      base::TimeDelta::FromSeconds(999999),
      base::Bind(&Metrics::ReportTimeOnTechnology, base::Unretained(&library_),
                 histogram_names, 10 /* histogram min (seconds) */,
                 1000 /* max */, cumulative_names));

  // Create two services, to be passed to NotifyDefaultServiceChange.
  scoped_refptr<MockService> cell_service(new MockService(&manager_));
  scoped_refptr<MockService> wifi_service(new MockService(&manager_));

  // Register both cellular and wifi devices.
  metrics_.RegisterDevice(kCellIndex, Technology::kCellular);
  metrics_.RegisterDevice(kWifiIndex, Technology::kWifi);

  // Set up mock elapsed times.
  std::vector<int> times = {1, 1, 1};
  cumulative_metrics->SetActiveTimes(times);

  // Accumulate samples.  The number of calls to AccumulateTimeOnTechnology
  // must not exceed the length of the |times| vector above.

  EXPECT_CALL(*wifi_service, technology()).WillOnce(Return(Technology::kWifi));
  metrics_.OnDefaultLogicalServiceChanged(wifi_service);
  Metrics::AccumulateTimeOnTechnology(&metrics_, cumulative_names,
                                      cumulative_metrics.get());

  EXPECT_CALL(*cell_service, technology())
      .WillOnce(Return(Technology::kCellular));
  metrics_.OnDefaultLogicalServiceChanged(cell_service);
  Metrics::AccumulateTimeOnTechnology(&metrics_, cumulative_names,
                                      cumulative_metrics.get());

  EXPECT_CALL(*wifi_service, technology()).WillOnce(Return(Technology::kWifi));
  metrics_.OnDefaultLogicalServiceChanged(wifi_service);
  Metrics::AccumulateTimeOnTechnology(&metrics_, cumulative_names,
                                      cumulative_metrics.get());
  EXPECT_CALL(library_, SendToUMA("Histogram.TimeOnlineAny", 3, _, _, _));
  EXPECT_CALL(library_, SendToUMA("Histogram.TimeOnlineCell", 1, _, _, _));
  EXPECT_CALL(library_, SendToUMA("Histogram.TimeOnlineWifi", 2, _, _, _));
  EXPECT_CALL(library_,
              SendEnumToUMA("Histogram.FractionOnlineCell", 1 * 100 / 3, _));
  EXPECT_CALL(library_,
              SendEnumToUMA("Histogram.FractionOnlineWifi", 2 * 100 / 3, _));
  Metrics::ReportTimeOnTechnology(&library_, histogram_names, 0, 100,
                                  cumulative_names, cumulative_metrics.get());
}

TEST_F(MetricsTest, NotifyNeighborLinkMonitorFailure) {
  using NeighborSignal = patchpanel::NeighborReachabilityEventSignal;
  const std::string histogram = "Network.Shill.Wifi.NeighborLinkMonitorFailure";

  EXPECT_CALL(library_,
              SendEnumToUMA(histogram, Metrics::kNeighborIPv4GatewayFailure,
                            Metrics::kNeighborLinkMonitorFailureMax));
  metrics_.NotifyNeighborLinkMonitorFailure(
      Technology::kWifi, IPAddress::kFamilyIPv4, NeighborSignal::GATEWAY);

  EXPECT_CALL(library_,
              SendEnumToUMA(histogram, Metrics::kNeighborIPv4DNSServerFailure,
                            Metrics::kNeighborLinkMonitorFailureMax));
  metrics_.NotifyNeighborLinkMonitorFailure(
      Technology::kWifi, IPAddress::kFamilyIPv4, NeighborSignal::DNS_SERVER);

  EXPECT_CALL(
      library_,
      SendEnumToUMA(histogram, Metrics::kNeighborIPv4GatewayAndDNSServerFailure,
                    Metrics::kNeighborLinkMonitorFailureMax));
  metrics_.NotifyNeighborLinkMonitorFailure(
      Technology::kWifi, IPAddress::kFamilyIPv4,
      NeighborSignal::GATEWAY_AND_DNS_SERVER);

  EXPECT_CALL(library_,
              SendEnumToUMA(histogram, Metrics::kNeighborIPv6GatewayFailure,
                            Metrics::kNeighborLinkMonitorFailureMax));
  metrics_.NotifyNeighborLinkMonitorFailure(
      Technology::kWifi, IPAddress::kFamilyIPv6, NeighborSignal::GATEWAY);

  EXPECT_CALL(library_,
              SendEnumToUMA(histogram, Metrics::kNeighborIPv6DNSServerFailure,
                            Metrics::kNeighborLinkMonitorFailureMax));
  metrics_.NotifyNeighborLinkMonitorFailure(
      Technology::kWifi, IPAddress::kFamilyIPv6, NeighborSignal::DNS_SERVER);

  EXPECT_CALL(
      library_,
      SendEnumToUMA(histogram, Metrics::kNeighborIPv6GatewayAndDNSServerFailure,
                    Metrics::kNeighborLinkMonitorFailureMax));
  metrics_.NotifyNeighborLinkMonitorFailure(
      Technology::kWifi, IPAddress::kFamilyIPv6,
      NeighborSignal::GATEWAY_AND_DNS_SERVER);
}

#ifndef NDEBUG

// We don't need the extra thread inside EventDispatcherForTest, using
// ::testing::Test here directly.
using MetricsDeathTest = ::testing::Test;

TEST_F(MetricsDeathTest, PortalDetectionResultToEnumDNSSuccess) {
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS;
  result.http_status = PortalDetector::Status::kSuccess;
  EXPECT_DEATH(Metrics::PortalDetectionResultToEnum(result),
               "Final result status 1 is not allowed in the DNS phase");
}

TEST_F(MetricsDeathTest, PortalDetectionResultToEnumConnectionSuccess) {
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kConnection;
  result.http_status = PortalDetector::Status::kSuccess;
  EXPECT_DEATH(Metrics::PortalDetectionResultToEnum(result),
               "Final result status 1 is not allowed in the Connection phase");
}

TEST_F(MetricsDeathTest, PortalDetectionResultToEnumHTTPSuccess) {
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kHTTP;
  result.http_status = PortalDetector::Status::kSuccess;
  EXPECT_DEATH(Metrics::PortalDetectionResultToEnum(result),
               "Final result status 1 is not allowed in the HTTP phase");
}

#endif  // NDEBUG

}  // namespace shill
