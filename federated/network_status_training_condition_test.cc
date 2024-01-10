// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <shill/dbus-constants.h>
#include <shill/dbus-proxies.h>
#include <shill/dbus-proxy-mocks.h>

#include "federated/fake_shill_proxy.h"
#include "federated/network_status_training_condition.h"

namespace federated {
namespace {

using org::chromium::flimflam::ManagerProxyMock;
using org::chromium::flimflam::ServiceProxyInterface;
using org::chromium::flimflam::ServiceProxyMock;

using ::shill::kFlimflamManagerInterface;
using ::shill::kMonitorPropertyChanged;
using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr char kValidShillService1[] = "/fake/valid/service1";
constexpr char kValidShillService2[] = "/fake/valid/service2";
constexpr char kValidShillService3[] = "/fake/valid/service3";
constexpr char kInValidShillService[] = "/";

}  // namespace

class NetworkStatusTrainingConditionTest : public ::testing::Test {
 public:
  NetworkStatusTrainingConditionTest() = default;

  NetworkStatusTrainingConditionTest(
      const NetworkStatusTrainingConditionTest&) = delete;
  NetworkStatusTrainingConditionTest& operator=(
      const NetworkStatusTrainingConditionTest&) = delete;

  void SetUp() override {
    fake_shill_proxy_ = new FakeShillProxy();
    ManagerProxyMock* shill_manager_proxy_mock =
        fake_shill_proxy_->GetShillManagerProxy();
    // Saves the callback for simulating PropertyChanged signals.
    EXPECT_CALL(*shill_manager_proxy_mock,
                DoRegisterPropertyChangedSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&shill_manager_property_changed_callback_));

    // Initializes manager_proxy's properties with default service path, and
    // initializes the default service with metered = false.

    brillo::VariantDictionary manager_proxy_properties_dict;
    manager_proxy_properties_dict[shill::kDefaultServiceProperty] =
        dbus::ObjectPath(kValidShillService1);

    EXPECT_CALL(*shill_manager_proxy_mock, GetProperties(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(manager_proxy_properties_dict),
                        Return(true)));

    // The return value of service_proxy GetProperties call.
    brillo::VariantDictionary service_proxy_properties_dict;
    service_proxy_properties_dict[shill::kMeteredProperty] = false;

    ServiceProxyMock* shill_service_proxy_mock = new ServiceProxyMock();
    fake_shill_proxy_->SetServiceProxyForPath(
        kValidShillService1,
        std::unique_ptr<ServiceProxyInterface>(shill_service_proxy_mock));

    EXPECT_CALL(*shill_service_proxy_mock, GetProperties(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(service_proxy_properties_dict),
                        Return(true)));

    network_status_training_condition_ =
        std::make_unique<NetworkStatusTrainingCondition>(fake_shill_proxy_);
  }

 protected:
  FakeShillProxy* fake_shill_proxy_;

  std::unique_ptr<NetworkStatusTrainingCondition>
      network_status_training_condition_;

  base::RepeatingCallback<void(const std::string&, const brillo::Any&)>
      shill_manager_property_changed_callback_;
};

// Tests that NetworkStatusTrainingCondition correctly request the network
// status on initialization.
TEST_F(NetworkStatusTrainingConditionTest, InitializedValue) {
  EXPECT_TRUE(network_status_training_condition_
                  ->IsTrainingConditionSatisfiedToStart());
}

// Tests when shill ManagerProxy's PropertyChanges signal has the default
// service path unchanged, the signal is ignored.
TEST_F(NetworkStatusTrainingConditionTest, DefaultServicePropertyUnchanged) {
  shill_manager_property_changed_callback_.Run(
      shill::kDefaultServiceProperty, dbus::ObjectPath(kValidShillService1));
  EXPECT_TRUE(network_status_training_condition_
                  ->IsTrainingConditionSatisfiedToStart());
}

// Tests when shill ManagerProxy's PropertyChanges signal has an invalid default
// service path, we treat it non metered.
TEST_F(NetworkStatusTrainingConditionTest, DefaultServicePropertyInvalid) {
  shill_manager_property_changed_callback_.Run(
      shill::kDefaultServiceProperty, dbus::ObjectPath(kInValidShillService));

  EXPECT_TRUE(network_status_training_condition_
                  ->IsTrainingConditionSatisfiedToStart());
}

// Tests that when shill service proxy GetProperties() call fails, we treat it
// non meterred.
TEST_F(NetworkStatusTrainingConditionTest, ServicePropertyNoResponse) {
  ServiceProxyMock* shill_service_proxy_mock = new ServiceProxyMock();
  fake_shill_proxy_->SetServiceProxyForPath(
      kValidShillService2,
      std::unique_ptr<ServiceProxyInterface>(shill_service_proxy_mock));

  EXPECT_CALL(*shill_service_proxy_mock, GetProperties(_, _, _))
      .WillOnce(Return(false));

  shill_manager_property_changed_callback_.Run(
      shill::kDefaultServiceProperty, dbus::ObjectPath(kValidShillService2));

  EXPECT_TRUE(network_status_training_condition_
                  ->IsTrainingConditionSatisfiedToStart());
}

TEST_F(NetworkStatusTrainingConditionTest, DefaultServicePropertyChanged) {
  // Sets the service proty properties with metered = true.
  brillo::VariantDictionary service_proxy_properties_dict;
  service_proxy_properties_dict[shill::kMeteredProperty] = true;

  // Prepares the new shill service proxy.
  ServiceProxyMock* shill_service_proxy_mock_2 = new ServiceProxyMock();
  fake_shill_proxy_->SetServiceProxyForPath(
      kValidShillService2,
      std::unique_ptr<ServiceProxyInterface>(shill_service_proxy_mock_2));

  EXPECT_CALL(*shill_service_proxy_mock_2, GetProperties(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<0>(service_proxy_properties_dict), Return(true)));

  shill_manager_property_changed_callback_.Run(
      shill::kDefaultServiceProperty, dbus::ObjectPath(kValidShillService2));

  EXPECT_FALSE(network_status_training_condition_
                   ->IsTrainingConditionSatisfiedToStart());

  // Triggers another PropertyChanged signal with a different service path, this
  // time metered = true.
  service_proxy_properties_dict[shill::kMeteredProperty] = false;
  ServiceProxyMock* shill_service_proxy_mock_3 = new ServiceProxyMock();
  fake_shill_proxy_->SetServiceProxyForPath(
      kValidShillService3,
      std::unique_ptr<ServiceProxyInterface>(shill_service_proxy_mock_3));

  EXPECT_CALL(*shill_service_proxy_mock_3, GetProperties(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<0>(service_proxy_properties_dict), Return(true)));

  shill_manager_property_changed_callback_.Run(
      shill::kDefaultServiceProperty, dbus::ObjectPath(kValidShillService3));

  EXPECT_TRUE(network_status_training_condition_
                  ->IsTrainingConditionSatisfiedToStart());
}

}  // namespace federated
