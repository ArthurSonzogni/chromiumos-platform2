// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/connection_manager.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxies.h>
#include <shill/dbus-proxy-mocks.h>

#include "update_engine/common/test_utils.h"
#include "update_engine/cros/fake_shill_proxy.h"
#include "update_engine/cros/fake_system_state.h"

using chromeos_update_engine::connection_utils::StringForConnectionType;
using org::chromium::flimflam::ManagerProxyMock;
using org::chromium::flimflam::ServiceProxyMock;
using std::set;
using std::string;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace chromeos_update_engine {

class ConnectionManagerTest : public ::testing::Test {
 public:
  ConnectionManagerTest() : fake_shill_proxy_(new FakeShillProxy()) {}

  void SetUp() override {
    loop_.SetAsCurrent();
    FakeSystemState::CreateInstance();
    FakeSystemState::Get()->set_connection_manager(&cmut_);
  }

  void TearDown() override { EXPECT_FALSE(loop_.PendingTasks()); }

 protected:
  // Sets the default_service object path in the response from the
  // ManagerProxyMock instance.
  void SetManagerReply(const char* default_service, bool reply_succeeds);

  // Sets the `service_type`, `physical_technology` and `service_metered`
  // properties in the mocked service `service_path`. If any of the two const
  // char* is a nullptr, the corresponding property will not be included in the
  // response.
  void SetServiceReply(const string& service_path,
                       const char* service_type,
                       const char* physical_technology,
                       bool service_metered);

  void TestWithServiceType(const char* service_type,
                           const char* physical_technology,
                           ConnectionType expected_type);

  void TestWithServiceDisconnected(ConnectionType expected_type);

  void TestWithServiceMetered(bool service_metered, bool expected_metered);

  brillo::FakeMessageLoop loop_{nullptr};
  FakeShillProxy* fake_shill_proxy_;

  // ConnectionManager under test.
  ConnectionManager cmut_{fake_shill_proxy_};
};

void ConnectionManagerTest::SetManagerReply(const char* default_service,
                                            bool reply_succeeds) {
  ManagerProxyMock* manager_proxy_mock = fake_shill_proxy_->GetManagerProxy();
  if (!reply_succeeds) {
    EXPECT_CALL(*manager_proxy_mock, GetProperties(_, _, _))
        .WillOnce(Return(false));
    return;
  }

  // Create a dictionary of properties and optionally include the default
  // service.
  brillo::VariantDictionary reply_dict;
  reply_dict["SomeOtherProperty"] = 0xC0FFEE;

  if (default_service) {
    reply_dict[shill::kDefaultServiceProperty] =
        dbus::ObjectPath(default_service);
  }
  EXPECT_CALL(*manager_proxy_mock, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(reply_dict), Return(true)));
}

void ConnectionManagerTest::SetServiceReply(const string& service_path,
                                            const char* service_type,
                                            const char* physical_technology,
                                            bool service_metered) {
  brillo::VariantDictionary reply_dict;
  reply_dict["SomeOtherProperty"] = 0xC0FFEE;

  if (service_type) {
    reply_dict[shill::kTypeProperty] = string(service_type);
  }

  if (physical_technology) {
    reply_dict[shill::kPhysicalTechnologyProperty] =
        string(physical_technology);
  }

  reply_dict[shill::kMeteredProperty] = service_metered;

  std::unique_ptr<ServiceProxyMock> service_proxy_mock(new ServiceProxyMock());

  // Plumb return value into mock object.
  EXPECT_CALL(*service_proxy_mock.get(), GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(reply_dict), Return(true)));

  fake_shill_proxy_->SetServiceForPath(dbus::ObjectPath(service_path),
                                       std::move(service_proxy_mock));
}

void ConnectionManagerTest::TestWithServiceType(const char* service_type,
                                                const char* physical_technology,
                                                ConnectionType expected_type) {
  SetManagerReply("/service/guest/network", true);
  SetServiceReply("/service/guest/network", service_type, physical_technology,
                  false);

  ConnectionType type;
  bool metered = false;
  EXPECT_TRUE(cmut_.GetConnectionProperties(&type, &metered));
  EXPECT_EQ(expected_type, type);
  testing::Mock::VerifyAndClearExpectations(
      fake_shill_proxy_->GetManagerProxy());
}

void ConnectionManagerTest::TestWithServiceMetered(bool service_metered,
                                                   bool expected_metered) {
  SetManagerReply("/service/guest/network", true);
  SetServiceReply("/service/guest/network", shill::kTypeWifi, nullptr,
                  service_metered);

  ConnectionType type;
  bool metered = false;
  EXPECT_TRUE(cmut_.GetConnectionProperties(&type, &metered));
  EXPECT_EQ(expected_metered, metered);
  testing::Mock::VerifyAndClearExpectations(
      fake_shill_proxy_->GetManagerProxy());
}

void ConnectionManagerTest::TestWithServiceDisconnected(
    ConnectionType expected_type) {
  SetManagerReply("/", true);

  ConnectionType type;
  bool metered = false;
  EXPECT_TRUE(cmut_.GetConnectionProperties(&type, &metered));
  EXPECT_EQ(expected_type, type);
  testing::Mock::VerifyAndClearExpectations(
      fake_shill_proxy_->GetManagerProxy());
}

TEST_F(ConnectionManagerTest, SimpleTest) {
  TestWithServiceType(shill::kTypeEthernet, nullptr, ConnectionType::kEthernet);
  TestWithServiceType(shill::kTypeWifi, nullptr, ConnectionType::kWifi);
  TestWithServiceType(shill::kTypeCellular, nullptr, ConnectionType::kCellular);
}

TEST_F(ConnectionManagerTest, PhysicalTechnologyTest) {
  TestWithServiceType(shill::kTypeVPN, nullptr, ConnectionType::kUnknown);
  TestWithServiceType(shill::kTypeVPN, shill::kTypeVPN,
                      ConnectionType::kUnknown);
  TestWithServiceType(shill::kTypeVPN, shill::kTypeWifi, ConnectionType::kWifi);
}

TEST_F(ConnectionManagerTest, MeteredTest) {
  TestWithServiceMetered(/*service_metered=*/true, /*expected_metered=*/true);
  TestWithServiceMetered(/*service_metered=*/false, /*expected_metered=*/false);
}

TEST_F(ConnectionManagerTest, UnknownTest) {
  TestWithServiceType("foo", nullptr, ConnectionType::kUnknown);
}

TEST_F(ConnectionManagerTest, DisconnectTest) {
  TestWithServiceDisconnected(ConnectionType::kDisconnected);
}

TEST_F(ConnectionManagerTest, AllowUpdatesOnlyOver3GPerPolicyTest) {
  policy::MockDevicePolicy allow_3g_policy;

  FakeSystemState::Get()->set_device_policy(&allow_3g_policy);

  // This test tests cellular (3G) being the only connection type being allowed.
  set<string> allowed_set;
  allowed_set.insert(StringForConnectionType(ConnectionType::kCellular));

  EXPECT_CALL(allow_3g_policy, GetAllowedConnectionTypesForUpdate(_))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<0>(allowed_set), Return(true)));

  EXPECT_TRUE(cmut_.IsUpdateAllowedOverMetered());
}

TEST_F(ConnectionManagerTest, AllowUpdatesOverMeteredNetworkByDefaultTest) {
  policy::MockDevicePolicy device_policy;
  // Set an empty device policy.
  FakeSystemState::Get()->set_device_policy(&device_policy);

  EXPECT_TRUE(cmut_.IsUpdateAllowedOverMetered());
}

TEST_F(ConnectionManagerTest, BlockUpdatesOver3GPerPolicyTest) {
  policy::MockDevicePolicy block_3g_policy;

  FakeSystemState::Get()->set_device_policy(&block_3g_policy);

  // Test that updates for 3G are blocked while updates are allowed
  // over several other types.
  set<string> allowed_set;
  allowed_set.insert(StringForConnectionType(ConnectionType::kEthernet));
  allowed_set.insert(StringForConnectionType(ConnectionType::kWifi));

  EXPECT_CALL(block_3g_policy, GetAllowedConnectionTypesForUpdate(_))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<0>(allowed_set), Return(true)));

  EXPECT_FALSE(cmut_.IsUpdateAllowedOverMetered());
}

TEST_F(ConnectionManagerTest, AllowUpdatesOver3GIfPolicyIsNotSet) {
  policy::MockDevicePolicy device_policy;

  FakeSystemState::Get()->set_device_policy(&device_policy);

  // Return false for GetAllowedConnectionTypesForUpdate and see
  // that updates are allowed as device policy is not set. Further
  // check is left to `OmahaRequestAction`.
  EXPECT_CALL(device_policy, GetAllowedConnectionTypesForUpdate(_))
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_TRUE(cmut_.IsUpdateAllowedOverMetered());
}

TEST_F(ConnectionManagerTest, AllowUpdatesOverCellularIfPolicyFailsToBeLoaded) {
  FakeSystemState::Get()->set_device_policy(nullptr);

  EXPECT_TRUE(cmut_.IsUpdateAllowedOverMetered());
}

TEST_F(ConnectionManagerTest, StringForConnectionTypeTest) {
  EXPECT_STREQ(shill::kTypeEthernet,
               StringForConnectionType(ConnectionType::kEthernet));
  EXPECT_STREQ(shill::kTypeWifi,
               StringForConnectionType(ConnectionType::kWifi));
  EXPECT_STREQ(shill::kTypeCellular,
               StringForConnectionType(ConnectionType::kCellular));
  EXPECT_STREQ("Unknown", StringForConnectionType(ConnectionType::kUnknown));
  EXPECT_STREQ("Unknown",
               StringForConnectionType(static_cast<ConnectionType>(999999)));
}

TEST_F(ConnectionManagerTest, MalformedServiceList) {
  SetManagerReply("/service/guest/network", false);

  ConnectionType type;
  bool metered = false;
  EXPECT_FALSE(cmut_.GetConnectionProperties(&type, &metered));
}

}  // namespace chromeos_update_engine
