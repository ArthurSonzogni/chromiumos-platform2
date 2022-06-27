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

#include "shill/event_dispatcher.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/portal_detector.h"
#include "shill/routing_table.h"
#include "shill/service_under_test.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using ::testing::_;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

// This file contains Device unit tests focused on portal detection and
// integration with the PortalDetector class. These tests minimize the use of
// mocks, relying instead on a test PortalDetector implementation and a test
// Device implementation to provide the test PortalDetector.

// The primary advantage to this pattern, other than increased readability,
// is that it is much easier to test the Device state machine from
// StartPortalDetection() through completion, including multiple attempts.
// This will be especially helpful for ensuring that UMA metrics are properly
// measured.

namespace shill {

namespace {

const char kDeviceName[] = "testdevice";
const char kDeviceAddress[] = "00:01:02:03:04:05";
const int kDeviceInterfaceIndex = 1;

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
    started_ = true;
    num_attempts_++;
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

  void SetPortalSuspectedResult() {
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

  void Complete() {
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

}  // namespace

class DevicePortalDetectorTest : public testing::Test {
 public:
  DevicePortalDetectorTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        device_info_(&manager_) {
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
    service_ = new ServiceUnderTest(&manager_);
    service_->SetState(Service::kStateConnected);
    SetServiceCheckPortal(true);
    device_->SelectService(service_);
  }

  void PortalDetectorCallback(const PortalDetector::Result& result) {
    device_->PortalDetectorCallback(result);
  }

  TestPortalDetector* StartPortalDetection(bool restart = true) {
    device_->StartPortalDetection(restart);
    // This will be nullptr if StartPortalDetection() failed.
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

 protected:
  std::unique_ptr<MockConnection> CreateMockConnection() {
    auto connection = std::make_unique<NiceMock<MockConnection>>(&device_info_);
    const IPAddress ip_addr = IPAddress("192.168.86.2");
    EXPECT_CALL(*connection, local()).WillRepeatedly(ReturnRef(ip_addr));
    EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));
    const IPAddress gateway = IPAddress("192.168.86.1");
    EXPECT_CALL(*connection, gateway()).WillRepeatedly(ReturnRef(gateway));
    const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
    EXPECT_CALL(*connection, dns_servers()).WillRepeatedly(ReturnRef(dns_list));
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
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
  NiceMock<MockDeviceInfo> device_info_;
  scoped_refptr<TestDevice> device_;
  scoped_refptr<ServiceUnderTest> service_;
};

TEST_F(DevicePortalDetectorTest, Disabled) {
  SetServiceCheckPortal(false);

  TestPortalDetector* portal_detector = StartPortalDetection();
  EXPECT_FALSE(portal_detector);
}

TEST_F(DevicePortalDetectorTest, DNSFailure) {
  TestPortalDetector* portal_detector = StartPortalDetection();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetDNSResult(PortalDetector::Status::kFailure);
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateNoConnectivity);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 2);
}

TEST_F(DevicePortalDetectorTest, DNSTimeout) {
  TestPortalDetector* portal_detector = StartPortalDetection();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetDNSResult(PortalDetector::Status::kTimeout);
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateNoConnectivity);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 2);
}

TEST_F(DevicePortalDetectorTest, RedirectFound) {
  TestPortalDetector* portal_detector = StartPortalDetection();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetRedirectResult("http://www.redirect.com/signin");
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateRedirectFound);
  EXPECT_EQ(GetServiceProbeUrlString(),
            portal_detector->result().probe_url_string);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 2);
}

TEST_F(DevicePortalDetectorTest, RedirectFoundNoUrl) {
  TestPortalDetector* portal_detector = StartPortalDetection();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  // Redirect result with an empty redirect URL -> PortalSuspected state.
  portal_detector->SetRedirectResult("");
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 2);
}

TEST_F(DevicePortalDetectorTest, PortalSuspected) {
  TestPortalDetector* portal_detector = StartPortalDetection();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetPortalSuspectedResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 2);
}

TEST_F(DevicePortalDetectorTest, PortalSuspectedThenOnline) {
  TestPortalDetector* portal_detector = StartPortalDetection();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetPortalSuspectedResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStatePortalSuspected);

  // Portal detection should be started again.
  portal_detector = GetPortalDetector();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());
  EXPECT_EQ(portal_detector->num_attempts(), 2);

  // Completion with an 'online' result should set the Service state to online.
  portal_detector->SetOnlineResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateOnline);

  // Portal detection should be completed and the PortalDetector destroyed.
  EXPECT_FALSE(GetPortalDetector());
}

TEST_F(DevicePortalDetectorTest, Online) {
  TestPortalDetector* portal_detector = StartPortalDetection();
  ASSERT_TRUE(portal_detector);
  EXPECT_TRUE(portal_detector->IsInProgress());

  portal_detector->SetOnlineResult();
  portal_detector->Complete();
  EXPECT_EQ(service_->state(), Service::kStateOnline);

  // Portal detection should be completed and the PortalDetector destroyed.
  EXPECT_FALSE(GetPortalDetector());
}

}  // namespace shill
