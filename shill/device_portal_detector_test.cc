// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "metrics/fake_metrics_library.h"
#include "shill/event_dispatcher.h"
#include "shill/metrics.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/portal_detector.h"
#include "shill/routing_table.h"
#include "shill/service_under_test.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::ReturnRefOfCopy;

// This file contains Device unit tests focused on portal detection and
// integration with the PortalDetector class. These tests minimize the use of
// mocks, relying instead on a test PortalDetector implementation and a test
// Device implementation to provide the test PortalDetector.

// The primary advantage to this pattern, other than increased readability,
// is that it is much easier to test the Device state machine from
// UpdatePortalDetector() through completion, including multiple attempts.
// This will be especially helpful for ensuring that UMA metrics are properly
// measured.

namespace shill {

namespace {

const char kDeviceName[] = "testdevice";
const char kDeviceAddress[] = "00:01:02:03:04:05";
const int kDeviceInterfaceIndex = 1;
const char kRedirectUrl[] = "http://www.redirect.com/signin";

// Portal detection is technology agnostic, use 'unknown'.
const Technology kTestTechnology = Technology::kUnknown;

class TestPortalDetector : public PortalDetector {
 public:
  TestPortalDetector(EventDispatcher* dispatcher,
                     base::RepeatingCallback<void(const Result&)> callback)
      : PortalDetector(dispatcher, callback) {}
  ~TestPortalDetector() override = default;
  TestPortalDetector(const TestPortalDetector&) = delete;
  TestPortalDetector& operator=(const TestPortalDetector&) = delete;

  // PortalDetector overrides
  bool Start(const ManagerProperties& props,
             const std::string& ifname,
             const IPAddress& src_address,
             const std::vector<std::string>& dns_list,
             const std::string& logging_tag,
             base::TimeDelta delay = base::TimeDelta()) override {
    if (delay == base::TimeDelta()) {
      started_ = true;
      num_attempts_++;
    } else {
      delayed_ = true;
    }
    return true;
  }

  void Stop() override { started_ = false; }

  bool IsInProgress() override { return started_; };

  base::TimeDelta GetNextAttemptDelay() override {
    return base::Milliseconds(1);
  }

  void SetDNSResult(PortalDetector::Status status) {
    result_ = PortalDetector::Result();
    result_.http_phase = PortalDetector::Phase::kDNS;
    result_.http_status = status;
    result_.https_phase = PortalDetector::Phase::kDNS;
    result_.https_status = status;
  }

  void SetRedirectResult(const std::string& redirect_url) {
    result_ = PortalDetector::Result();
    result_.http_phase = PortalDetector::Phase::kContent;
    result_.http_status = PortalDetector::Status::kRedirect;
    result_.http_status_code = 302;
    result_.redirect_url_string = redirect_url;
    result_.https_phase = PortalDetector::Phase::kContent;
    result_.https_status = PortalDetector::Status::kSuccess;
  }

  void SetHTTPSFailureResult() {
    result_ = PortalDetector::Result();
    result_.http_phase = PortalDetector::Phase::kContent;
    result_.http_status = PortalDetector::Status::kSuccess;
    result_.http_status_code = 204;
    result_.https_phase = PortalDetector::Phase::kContent;
    result_.https_status = PortalDetector::Status::kFailure;
  }

  void SetOnlineResult() {
    result_ = PortalDetector::Result();
    result_.http_phase = PortalDetector::Phase::kContent;
    result_.http_status = PortalDetector::Status::kSuccess;
    result_.http_status_code = 204;
    result_.https_phase = PortalDetector::Phase::kContent;
    result_.https_status = PortalDetector::Status::kSuccess;
  }

  void Continue() {
    if (delayed_) {
      started_ = true;
      num_attempts_++;
      delayed_ = false;
    }
  }

  void Complete() {
    if (delayed_)
      Continue();
    started_ = false;
    // The callback might delete |this| so copy |result_|.
    PortalDetector::Result result = result_;
    result.num_attempts = num_attempts_;
    portal_result_callback().Run(result);
  }

  const PortalDetector::Result& result() const { return result_; }
  int num_attempts() { return num_attempts_; }

 private:
  PortalDetector::Result result_;
  bool started_ = false;
  bool delayed_ = false;
  int num_attempts_ = 0;
  base::WeakPtrFactory<TestPortalDetector> test_weak_ptr_factory_{this};
};

class TestDevice : public Device {
 public:
  TestDevice(Manager* manager,
             const std::string& link_name,
             const std::string& address,
             int interface_index,
             Technology technology)
      : Device(manager, link_name, address, interface_index, technology) {}
  ~TestDevice() override = default;

  // Device overrides
  void Start(Error* error,
             const EnabledStateChangedCallback& callback) override {}

  void Stop(Error* error,
            const EnabledStateChangedCallback& callback) override {}

  void StartConnectionDiagnosticsAfterPortalDetection() override {}

  std::unique_ptr<PortalDetector> CreatePortalDetector() override {
    return std::make_unique<TestPortalDetector>(
        dispatcher(),
        base::BindRepeating(&TestDevice::TestPortalDetectorCallback,
                            test_weak_ptr_factory_.GetWeakPtr()));
  }

  // A protected Device method can not be bound directly so use a wrapper.
  void TestPortalDetectorCallback(const PortalDetector::Result& result) {
    PortalDetectorCallback(result);
  }

  TestPortalDetector* test_portal_detector() {
    return static_cast<TestPortalDetector*>(portal_detector());
  }

 private:
  base::WeakPtrFactory<TestDevice> test_weak_ptr_factory_{this};
};

class TestService : public ServiceUnderTest {
 public:
  explicit TestService(Manager* manager) : ServiceUnderTest(manager) {}
  ~TestService() override = default;

 protected:
  // Service
  void OnConnect(Error* /*error*/) override { SetState(kStateConnected); }

  void OnDisconnect(Error* /*error*/, const char* /*reason*/) override {
    SetState(kStateIdle);
  }
};

}  // namespace

class DevicePortalDetectorTest : public testing::Test {
 public:
  DevicePortalDetectorTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        device_info_(&manager_) {
    metrics_.SetLibraryForTesting(&fake_metrics_library_);
    manager_.set_mock_device_info(&device_info_);
    device_ = new TestDevice(&manager_, kDeviceName, kDeviceAddress,
                             kDeviceInterfaceIndex, kTestTechnology);
  }

  ~DevicePortalDetectorTest() override = default;

  void SetUp() override {
    RoutingTable::GetInstance()->Start();

    ManagerProperties props = GetManagerPortalProperties();
    EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));

    device_->network_->set_connection_for_testing(CreateMockConnection());

    // Set up a connected test Service for the Device.
    service_ = new TestService(&manager_);
    service_->SetState(Service::kStateConnected);
    SetServiceCheckPortal(true);
    device_->SelectService(service_);
  }

  void PortalDetectorCallback(const PortalDetector::Result& result) {
    device_->PortalDetectorCallback(result);
  }

  TestPortalDetector* UpdatePortalDetector(bool restart = true) {
    device_->UpdatePortalDetector(restart);
    // This will be nullptr if UpdatePortalDetector() did not start detection.
    return device_->test_portal_detector();
  }

  TestPortalDetector* GetPortalDetector() {
    return device_->test_portal_detector();
  }

  void SetServiceCheckPortal(bool check_portal) {
    service_->SetCheckPortal(
        check_portal ? Service::kCheckPortalTrue : Service::kCheckPortalFalse,
        /*error=*/nullptr);
  }

  std::string GetServiceProbeUrlString() { return service_->probe_url_string_; }

  int NumHistogramCalls(
      const Metrics::HistogramMetric<Metrics::NameByTechnology>& metric) {
    return fake_metrics_library_.NumCalls(Metrics::GetFullMetricName(
        metric.n.name, kTestTechnology, metric.n.location));
  }

  int NumEnumMetricsCalls(
      const Metrics::EnumMetric<Metrics::NameByTechnology>& metric) {
    return fake_metrics_library_.NumCalls(Metrics::GetFullMetricName(
        metric.n.name, kTestTechnology, metric.n.location));
  }

  std::vector<int> MetricsHistogramCalls(
      const Metrics::HistogramMetric<Metrics::NameByTechnology>& metric) {
    return fake_metrics_library_.GetCalls(Metrics::GetFullMetricName(
        metric.n.name, kTestTechnology, metric.n.location));
  }

  std::vector<int> MetricsEnumCalls(
      const Metrics::EnumMetric<Metrics::NameByTechnology>& metric) {
    return fake_metrics_library_.GetCalls(Metrics::GetFullMetricName(
        metric.n.name, kTestTechnology, metric.n.location));
  }

 protected:
  std::unique_ptr<MockConnection> CreateMockConnection() {
    auto connection = std::make_unique<NiceMock<MockConnection>>(&device_info_);
    const IPAddress ip_addr = IPAddress("192.168.86.2");
    EXPECT_CALL(*connection, local()).WillRepeatedly(ReturnRefOfCopy(ip_addr));
    EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));
    const IPAddress gateway = IPAddress("192.168.86.1");
    EXPECT_CALL(*connection, gateway())
        .WillRepeatedly(ReturnRefOfCopy(gateway));
    const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
    EXPECT_CALL(*connection, dns_servers())
        .WillRepeatedly(ReturnRefOfCopy(dns_list));
    return connection;
  }

  ManagerProperties GetManagerPortalProperties() {
    ManagerProperties props;
    props.portal_http_url = PortalDetector::kDefaultHttpUrl;
    props.portal_https_url = PortalDetector::kDefaultHttpsUrl;
    props.portal_fallback_http_urls = std::vector<std::string>(
        PortalDetector::kDefaultFallbackHttpUrls.begin(),
        PortalDetector::kDefaultFallbackHttpUrls.end());
    return props;
  }

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  Metrics metrics_;
  FakeMetricsLibrary fake_metrics_library_;
  NiceMock<MockManager> manager_;
  NiceMock<MockDeviceInfo> device_info_;
  scoped_refptr<TestDevice> device_;
  scoped_refptr<TestService> service_;
};

TEST_F(DevicePortalDetectorTest, Disabled) {
  SetServiceCheckPortal(false);

  TestPortalDetector* portal_detector = UpdatePortalDetector();
  EXPECT_FALSE(portal_detector);
}

TEST_F(DevicePortalDetectorTest, DNSFailure) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetDNSResult(PortalDetector::Status::kFailure);
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateNoConnectivity);

  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultDNSFailure));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultDNSFailure));
  EXPECT_EQ(NumEnumMetricsCalls(Metrics::kPortalDetectorRetryResult), 0);

  EXPECT_EQ(NumHistogramCalls(Metrics::kMetricPortalAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_FALSE(portal_detector->IsInProgress());
  portal_detector->Continue();
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 2);
}

TEST_F(DevicePortalDetectorTest, DNSTimeout) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetDNSResult(PortalDetector::Status::kTimeout);
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateNoConnectivity);

  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultDNSTimeout));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultDNSTimeout));
  EXPECT_EQ(NumEnumMetricsCalls(Metrics::kPortalDetectorRetryResult), 0);

  EXPECT_EQ(NumHistogramCalls(Metrics::kMetricPortalAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);

  // Portal detection should still be active.
  EXPECT_TRUE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, RedirectFound) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetRedirectResult(kRedirectUrl);
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateRedirectFound);
  EXPECT_EQ(GetServiceProbeUrlString(),
            portal_detector->result().probe_url_string);

  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultContentRedirect));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultRedirectFound));
  EXPECT_EQ(NumEnumMetricsCalls(Metrics::kPortalDetectorRetryResult), 0);

  EXPECT_EQ(NumHistogramCalls(Metrics::kMetricPortalAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline), 0);
  EXPECT_THAT(
      MetricsHistogramCalls(Metrics::kPortalDetectorAttemptsToRedirectFound),
      ElementsAre(1));
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);

  // Portal detection should still be active.
  EXPECT_TRUE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, RedirectFoundNoUrl) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  // Redirect result with an empty redirect URL -> PortalSuspected state.
  portal_detector->SetRedirectResult("");
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultContentRedirect));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultRedirectNoUrl));
  EXPECT_EQ(NumEnumMetricsCalls(Metrics::kPortalDetectorRetryResult), 0);

  EXPECT_EQ(NumHistogramCalls(Metrics::kMetricPortalAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToRedirectFound),
            0);

  // Portal detection should still be active.
  EXPECT_TRUE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, RedirectFoundThenOnline) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetRedirectResult(kRedirectUrl);
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateRedirectFound);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  portal_detector->Continue();
  EXPECT_EQ(portal_detector->num_attempts(), 2);

  // Completion with an 'online' result should set the Service state to online.
  portal_detector->SetOnlineResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateOnline);

  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultContentRedirect,
                          Metrics::kPortalResultSuccess));
  EXPECT_THAT(MetricsHistogramCalls(Metrics::kMetricPortalAttemptsToOnline),
              ElementsAre(2));
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);

  // Portal detection should be completed and the PortalDetector destroyed.
  EXPECT_FALSE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, PortalSuspected) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetHTTPSFailureResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  // NOTE: Since we only report on the HTTP phase, a portal-suspected result
  // reports 'success'. This will be addressed when the metrics are updated.
  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultSuccess));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_EQ(NumEnumMetricsCalls(Metrics::kPortalDetectorRetryResult), 0);

  EXPECT_EQ(NumHistogramCalls(Metrics::kMetricPortalAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToRedirectFound),
            0);

  // Portal detection should still be active.
  EXPECT_TRUE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, PortalSuspectedThenRedirectFound) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  // Multiple portal-suspected results.
  portal_detector->SetHTTPSFailureResult();
  portal_detector->Complete();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);
  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultSuccess,
                          Metrics::kPortalResultSuccess));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorRetryResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_FALSE(portal_detector->IsInProgress());
  portal_detector->Continue();
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 3);

  // Completion with a 'redirect-found' result should set the Service state
  // to redirect-found and record the number of attempts..
  portal_detector->SetRedirectResult(kRedirectUrl);
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateRedirectFound);

  EXPECT_THAT(
      MetricsEnumCalls(Metrics::kMetricPortalResult),
      ElementsAre(Metrics::kPortalResultSuccess, Metrics::kPortalResultSuccess,
                  Metrics::kPortalResultContentRedirect));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorRetryResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure,
                          Metrics::kPortalDetectorResultRedirectFound));

  EXPECT_EQ(NumHistogramCalls(Metrics::kMetricPortalAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);
  EXPECT_THAT(
      MetricsHistogramCalls(Metrics::kPortalDetectorAttemptsToRedirectFound),
      ElementsAre(3));

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  portal_detector->Continue();
  EXPECT_EQ(portal_detector->num_attempts(), 4);
}

TEST_F(DevicePortalDetectorTest, PortalSuspectedThenOnline) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetHTTPSFailureResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  portal_detector->Continue();
  EXPECT_EQ(portal_detector->num_attempts(), 2);

  // Completion with an 'online' result should set the Service state to online.
  portal_detector->SetOnlineResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateOnline);

  // NOTE: Since we only report on the HTTP phase, a portal-suspected result
  // reports 'success'. This will be addressed when the metrics are updated.
  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultSuccess,
                          Metrics::kPortalResultSuccess));
  EXPECT_THAT(MetricsHistogramCalls(Metrics::kMetricPortalAttemptsToOnline),
              ElementsAre(2));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorRetryResult),
              ElementsAre(Metrics::kPortalDetectorResultOnline));

  EXPECT_THAT(MetricsHistogramCalls(Metrics::kMetricPortalAttemptsToOnline),
              ElementsAre(2));
  EXPECT_THAT(MetricsHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline),
              ElementsAre(2));
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);

  // Portal detection should be completed and the PortalDetector destroyed.
  EXPECT_FALSE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, PortalSuspectedThenDisconnect) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  // Multiple portal-suspected results
  portal_detector->SetHTTPSFailureResult();
  portal_detector->Complete();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  portal_detector->Continue();
  EXPECT_EQ(portal_detector->num_attempts(), 3);

  // Disconnect should not record an UMA result.
  service_->Disconnect(/*error=*/nullptr, /*reason=*/"test");
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateIdle);

  EXPECT_THAT(
      MetricsEnumCalls(Metrics::kMetricPortalResult),
      ElementsAre(Metrics::kPortalResultSuccess, Metrics::kPortalResultSuccess,
                  Metrics::kPortalResultSuccess));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorRetryResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));

  EXPECT_EQ(NumHistogramCalls(Metrics::kMetricPortalAttemptsToOnline), 0);
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline), 0);

  // Histogram records the number of failed attempts *before* a disconnect.
  EXPECT_THAT(
      MetricsHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect),
      ElementsAre(2));
}

TEST_F(DevicePortalDetectorTest, Online) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetOnlineResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateOnline);

  EXPECT_THAT(MetricsEnumCalls(Metrics::kMetricPortalResult),
              ElementsAre(Metrics::kPortalResultSuccess));
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultOnline));
  EXPECT_EQ(NumEnumMetricsCalls(Metrics::kPortalDetectorRetryResult), 0);

  EXPECT_THAT(MetricsHistogramCalls(Metrics::kMetricPortalAttemptsToOnline),
              ElementsAre(1));
  EXPECT_THAT(MetricsHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline),
              ElementsAre(1));
  EXPECT_EQ(NumHistogramCalls(Metrics::kPortalDetectorAttemptsToDisconnect), 0);

  // Portal detection should be completed and the PortalDetector destroyed.
  EXPECT_FALSE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, RestartPortalDetection) {
  TestPortalDetector* portal_detector = UpdatePortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  // Run portal detection 3 times.
  portal_detector->SetHTTPSFailureResult();
  portal_detector->Complete();
  portal_detector->Complete();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  portal_detector->Continue();
  EXPECT_TRUE(portal_detector->IsInProgress());

  // UpdatePortalDetector(true) will reset the current portal detector and
  // start a new one.
  device_->UpdatePortalDetector(/*restart=*/true);
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  // Complete will run portal detection 1 more time with an 'online' result.
  portal_detector->SetOnlineResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateOnline);

  // Old result metric gets called 4 times, with a final result of 'online'.
  // NOTE: Since we only report on the HTTP phase, a portal-suspected result
  // reports 'success'. This will be addressed when the metrics are updated.
  EXPECT_THAT(
      MetricsEnumCalls(Metrics::kMetricPortalResult),
      ElementsAre(Metrics::kPortalResultSuccess, Metrics::kPortalResultSuccess,
                  Metrics::kPortalResultSuccess,
                  Metrics::kPortalResultSuccess));
  // New initial result metric gets called once with an HTTPS failure.
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorInitialResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure));
  // New retry result metric gets called three times, ending with 'online'
  EXPECT_THAT(MetricsEnumCalls(Metrics::kPortalDetectorRetryResult),
              ElementsAre(Metrics::kPortalDetectorResultHTTPSFailure,
                          Metrics::kPortalDetectorResultHTTPSFailure,
                          Metrics::kPortalDetectorResultOnline));

  // Old attempts-to-online metric gets called once with a value of 1.
  EXPECT_THAT(MetricsHistogramCalls(Metrics::kMetricPortalAttemptsToOnline),
              ElementsAre(1));
  // New attempts-to-online metric gets called once with a value of 3+1 = 4.
  EXPECT_THAT(MetricsHistogramCalls(Metrics::kPortalDetectorAttemptsToOnline),
              ElementsAre(4));

  // Portal detection should be completed and the PortalDetector destroyed.
  EXPECT_FALSE(GetPortalDetector());
}

}  // namespace shill
