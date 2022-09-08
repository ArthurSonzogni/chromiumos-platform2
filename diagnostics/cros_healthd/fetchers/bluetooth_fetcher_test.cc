// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/dbus_bindings/bluetooth/dbus-proxy-mocks.h"

namespace diagnostics {
namespace {

using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

std::unique_ptr<org::bluez::Adapter1Proxy::PropertySet> GetAdapterProperties() {
  auto properties = std::make_unique<org::bluez::Adapter1Proxy::PropertySet>(
      nullptr, base::BindRepeating([](const std::string& property_name) {}));
  properties->address.ReplaceValue("aa:bb:cc:dd:ee:ff");
  properties->name.ReplaceValue("sarien-laptop");
  properties->powered.ReplaceValue(true);
  properties->discoverable.ReplaceValue(true);
  properties->discovering.ReplaceValue(true);
  properties->uuids.ReplaceValue({"0000110e-0000-1000-8000-00805f9b34fb",
                                  "0000111f-0000-1000-8000-00805f9b34fb",
                                  "0000110c-0000-1000-8000-00805f9b34fb"});
  properties->modalias.ReplaceValue("bluetooth:v00E0pC405d0067");
  return properties;
}

std::unique_ptr<org::bluez::Device1Proxy::PropertySet> GetDeviceProperties() {
  auto properties = std::make_unique<org::bluez::Device1Proxy::PropertySet>(
      nullptr, base::BindRepeating([](const std::string& property_name) {}));
  properties->connected.ReplaceValue(true);
  properties->address.ReplaceValue("70:88:6B:92:34:70");
  properties->name.ReplaceValue("GID6B");
  properties->type.ReplaceValue("BR/EDR");
  properties->appearance.ReplaceValue(2371);
  properties->modalias.ReplaceValue("bluetooth:v000ApFFFFdFFFF");
  properties->rssi.ReplaceValue(11822);
  properties->mtu.ReplaceValue(12320);
  properties->uuids.ReplaceValue({"00001107-d102-11e1-9b23-00025b00a5a5",
                                  "0000110c-0000-1000-8000-00805f9b34fb",
                                  "0000110e-0000-1000-8000-00805f9b34fb",
                                  "0000111e-0000-1000-8000-00805f9b34fb",
                                  "f8d1fbe4-7966-4334-8024-ff96c9330e15"});
  return properties;
}

std::unique_ptr<org::bluez::AdminPolicyStatus1Proxy::PropertySet>
GetAdapterPolicyProperties() {
  auto properties =
      std::make_unique<org::bluez::AdminPolicyStatus1Proxy::PropertySet>(
          nullptr,
          base::BindRepeating([](const std::string& property_name) {}));
  properties->service_allow_list.ReplaceValue(
      {"0000110b-0000-1000-8000-00805f9b34fb",
       "0000110d-0000-1000-8000-00805f9b34fb"});
  return properties;
}

std::unique_ptr<org::bluez::LEAdvertisingManager1Proxy::PropertySet>
GetAdapterAdvertisingProperties() {
  auto properties =
      std::make_unique<org::bluez::LEAdvertisingManager1Proxy::PropertySet>(
          nullptr,
          base::BindRepeating([](const std::string& property_name) {}));

  properties->supported_capabilities.ReplaceValue(
      {{"MaxAdvLen", static_cast<uint8_t>(31)},
       {"MaxScnRspLen", static_cast<uint8_t>(31)},
       {"MinTxPower", static_cast<int16_t>(-34)},
       {"MaxTxPower", static_cast<int16_t>(7)}});
  return properties;
}

std::unique_ptr<org::bluez::Battery1Proxy::PropertySet>
GetDeviceBatteryProperties() {
  auto properties = std::make_unique<org::bluez::Battery1Proxy::PropertySet>(
      nullptr, base::BindRepeating([](const std::string& property_name) {}));
  properties->percentage.ReplaceValue(80);
  return properties;
}

class MockBluezInfoManager final : public BluezInfoManager {
 public:
  MockBluezInfoManager() = default;
  MockBluezInfoManager(const MockBluezInfoManager&) = delete;
  MockBluezInfoManager& operator=(const MockBluezInfoManager&) = delete;
  ~MockBluezInfoManager() = default;

  MOCK_METHOD(std::vector<org::bluez::Adapter1ProxyInterface*>,
              adapters,
              (),
              (override));
  MOCK_METHOD(std::vector<org::bluez::Device1ProxyInterface*>,
              devices,
              (),
              (override));
  MOCK_METHOD(std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*>,
              admin_policies,
              (),
              (override));
  MOCK_METHOD(std::vector<org::bluez::LEAdvertisingManager1ProxyInterface*>,
              advertisings,
              (),
              (override));
  MOCK_METHOD(std::vector<org::bluez::Battery1ProxyInterface*>,
              batteries,
              (),
              (override));
};

class BluetoothUtilsTest : public ::testing::Test {
 protected:
  BluetoothUtilsTest() = default;
  BluetoothUtilsTest(const BluetoothUtilsTest&) = delete;
  BluetoothUtilsTest& operator=(const BluetoothUtilsTest&) = delete;
  ~BluetoothUtilsTest() = default;

  BluetoothFetcher* bluetooth_fetcher() { return &bluetooth_fetcher_; }
  const dbus::ObjectPath& adapter_path() { return adapter_path_; }
  const dbus::ObjectPath& device_path() { return device_path_; }

  // Getter of mock proxy.
  org::bluez::Adapter1ProxyMock* mock_adapter_proxy() const {
    return static_cast<testing::StrictMock<org::bluez::Adapter1ProxyMock>*>(
        adapter_proxy_.get());
  }
  org::bluez::Device1ProxyMock* mock_device_proxy() const {
    return static_cast<testing::StrictMock<org::bluez::Device1ProxyMock>*>(
        device_proxy_.get());
  }
  org::bluez::AdminPolicyStatus1ProxyMock* mock_admin_policy_proxy() const {
    return static_cast<
        testing::StrictMock<org::bluez::AdminPolicyStatus1ProxyMock>*>(
        admin_policy_proxy_.get());
  }
  org::bluez::LEAdvertisingManager1ProxyMock* mock_advertising_proxy() const {
    return static_cast<
        testing::StrictMock<org::bluez::LEAdvertisingManager1ProxyMock>*>(
        advertising_proxy_.get());
  }
  org::bluez::Battery1ProxyMock* mock_battery_proxy() const {
    return static_cast<testing::StrictMock<org::bluez::Battery1ProxyMock>*>(
        battery_proxy_.get());
  }

  // Set up function call in mock object.
  void SetMockAdapterProxyCall(
      const std::unique_ptr<org::bluez::Adapter1Proxy::PropertySet>&
          adapter_properties) {
    EXPECT_CALL(*mock_adapter_proxy(), name())
        .WillOnce(ReturnRef(adapter_properties->name.value()));
    EXPECT_CALL(*mock_adapter_proxy(), address())
        .WillOnce(ReturnRef(adapter_properties->address.value()));
    EXPECT_CALL(*mock_adapter_proxy(), powered())
        .WillOnce(Return(adapter_properties->powered.value()));
    EXPECT_CALL(*mock_adapter_proxy(), discoverable())
        .WillOnce(Return(adapter_properties->discoverable.value()));
    EXPECT_CALL(*mock_adapter_proxy(), discovering())
        .WillOnce(Return(adapter_properties->discovering.value()));
    EXPECT_CALL(*mock_adapter_proxy(), uuids())
        .WillOnce(ReturnRef(adapter_properties->uuids.value()));
    EXPECT_CALL(*mock_adapter_proxy(), modalias())
        .WillOnce(ReturnRef(adapter_properties->modalias.value()));
    EXPECT_CALL(*mock_adapter_proxy(), GetObjectPath())
        .WillOnce(ReturnRef(adapter_path()));
  }
  void SetMockDeviceProxyCall(
      const std::unique_ptr<org::bluez::Device1Proxy::PropertySet>&
          device_properties,
      int device_call_times) {
    EXPECT_CALL(*mock_device_proxy(), connected())
        .Times(device_call_times)
        .WillRepeatedly(Return(device_properties->connected.value()));
    EXPECT_CALL(*mock_device_proxy(), address())
        .Times(device_call_times)
        .WillRepeatedly(ReturnRef(device_properties->address.value()));
    EXPECT_CALL(*mock_device_proxy(), name())
        .Times(device_call_times)
        .WillRepeatedly(ReturnRef(device_properties->name.value()));
    EXPECT_CALL(*mock_device_proxy(), type())
        .Times(device_call_times)
        .WillRepeatedly(ReturnRef(device_properties->type.value()));
    EXPECT_CALL(*mock_device_proxy(), appearance())
        .Times(device_call_times)
        .WillRepeatedly(Return(device_properties->appearance.value()));
    EXPECT_CALL(*mock_device_proxy(), modalias())
        .Times(device_call_times)
        .WillRepeatedly(ReturnRef(device_properties->modalias.value()));
    EXPECT_CALL(*mock_device_proxy(), rssi())
        .Times(device_call_times)
        .WillRepeatedly(Return(device_properties->rssi.value()));
    EXPECT_CALL(*mock_device_proxy(), mtu())
        .Times(device_call_times)
        .WillRepeatedly(Return(device_properties->mtu.value()));
    EXPECT_CALL(*mock_device_proxy(), uuids())
        .Times(device_call_times)
        .WillRepeatedly(ReturnRef(device_properties->uuids.value()));
    EXPECT_CALL(*mock_device_proxy(), adapter())
        .Times(device_call_times)
        .WillRepeatedly(ReturnRef(adapter_path()));
    EXPECT_CALL(*mock_device_proxy(), GetObjectPath())
        .Times(device_call_times)
        .WillRepeatedly(ReturnRef(device_path()));
  }
  void SetMockOtherProxyCall(
      const std::unique_ptr<org::bluez::AdminPolicyStatus1Proxy::PropertySet>&
          admin_policy_properties,
      const std::unique_ptr<
          org::bluez::LEAdvertisingManager1Proxy::PropertySet>&
          advertising_properties,
      const std::unique_ptr<org::bluez::Battery1Proxy::PropertySet>&
          battery_properties) {
    // Admin Policy proxy.
    EXPECT_CALL(*mock_admin_policy_proxy(), GetObjectPath())
        .WillOnce(ReturnRef(adapter_path()));
    EXPECT_CALL(*mock_admin_policy_proxy(), service_allow_list())
        .WillOnce(
            ReturnRef(admin_policy_properties->service_allow_list.value()));
    // Advertising proxy.
    EXPECT_CALL(*mock_advertising_proxy(), GetObjectPath())
        .WillOnce(ReturnRef(adapter_path()));
    EXPECT_CALL(*mock_advertising_proxy(), supported_capabilities())
        .WillOnce(
            ReturnRef(advertising_properties->supported_capabilities.value()));
    // Battery proxy.
    EXPECT_CALL(*mock_battery_proxy(), GetObjectPath())
        .WillOnce(ReturnRef(device_path()));
    EXPECT_CALL(*mock_battery_proxy(), percentage())
        .WillOnce(Return(battery_properties->percentage.value()));
  }

 private:
  MockContext mock_context_;
  BluetoothFetcher bluetooth_fetcher_{&mock_context_};
  // Mock proxy.
  std::unique_ptr<org::bluez::Adapter1ProxyMock> adapter_proxy_ =
      std::make_unique<testing::StrictMock<org::bluez::Adapter1ProxyMock>>();
  std::unique_ptr<org::bluez::Device1ProxyMock> device_proxy_ =
      std::make_unique<testing::StrictMock<org::bluez::Device1ProxyMock>>();
  std::unique_ptr<org::bluez::AdminPolicyStatus1ProxyMock> admin_policy_proxy_ =
      std::make_unique<
          testing::StrictMock<org::bluez::AdminPolicyStatus1ProxyMock>>();
  std::unique_ptr<org::bluez::LEAdvertisingManager1ProxyMock>
      advertising_proxy_ = std::make_unique<
          testing::StrictMock<org::bluez::LEAdvertisingManager1ProxyMock>>();
  std::unique_ptr<org::bluez::Battery1ProxyMock> battery_proxy_ =
      std::make_unique<testing::StrictMock<org::bluez::Battery1ProxyMock>>();
  // Mock object path for Bluetooth adapter and device.
  const dbus::ObjectPath adapter_path_ = dbus::ObjectPath("/org/bluez/hci0");
  const dbus::ObjectPath device_path_ =
      dbus::ObjectPath("/org/bluez/dev_70_88_6B_92_34_70");
};

// Test that Bluetooth info can be fetched successfully.
TEST_F(BluetoothUtilsTest, FetchBluetoothInfo) {
  // Get mock data.
  const auto adapter_properties = GetAdapterProperties();
  const auto device_properties = GetDeviceProperties();
  const auto admin_policy_properties = GetAdapterPolicyProperties();
  const auto advertising_properties = GetAdapterAdvertisingProperties();
  const auto battery_properties = GetDeviceBatteryProperties();
  SetMockAdapterProxyCall(adapter_properties);
  SetMockDeviceProxyCall(device_properties, 1);
  SetMockOtherProxyCall(admin_policy_properties, advertising_properties,
                        battery_properties);

  auto mock_bluez_manager = std::make_unique<MockBluezInfoManager>();
  EXPECT_CALL(*mock_bluez_manager, adapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          mock_adapter_proxy()}));
  EXPECT_CALL(*mock_bluez_manager, devices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{
          mock_device_proxy()}));
  EXPECT_CALL(*mock_bluez_manager, admin_policies())
      .WillOnce(
          Return(std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*>{
              mock_admin_policy_proxy()}));
  EXPECT_CALL(*mock_bluez_manager, advertisings())
      .WillOnce(
          Return(std::vector<org::bluez::LEAdvertisingManager1ProxyInterface*>{
              mock_advertising_proxy()}));
  EXPECT_CALL(*mock_bluez_manager, batteries())
      .WillOnce(Return(std::vector<org::bluez::Battery1ProxyInterface*>{
          mock_battery_proxy()}));
  auto bluetooth_result =
      bluetooth_fetcher()->FetchBluetoothInfo(std::move(mock_bluez_manager));

  // Evaluate whether the information is correct or not.
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  ASSERT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->name, adapter_properties->name.value());
  EXPECT_EQ(adapter_info[0]->address, adapter_properties->address.value());
  EXPECT_TRUE(adapter_info[0]->powered);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 1);
  ASSERT_TRUE(adapter_info[0]->connected_devices.has_value());
  EXPECT_EQ(adapter_info[0]->connected_devices.value().size(), 1);
  EXPECT_EQ(adapter_info[0]->discoverable,
            adapter_properties->discoverable.value());
  EXPECT_EQ(adapter_info[0]->discovering,
            adapter_properties->discovering.value());
  ASSERT_TRUE(adapter_info[0]->uuids.has_value());
  EXPECT_EQ(adapter_info[0]->uuids, adapter_properties->uuids.value());
  ASSERT_TRUE(adapter_info[0]->modalias.has_value());
  EXPECT_EQ(adapter_info[0]->modalias, adapter_properties->modalias.value());
  EXPECT_EQ(adapter_info[0]->service_allow_list,
            admin_policy_properties->service_allow_list.value());

  const brillo::VariantDictionary adapter_capabilities_info = {
      {"MaxAdvLen", adapter_info[0]->supported_capabilities->max_adv_len},
      {"MaxScnRspLen",
       adapter_info[0]->supported_capabilities->max_scn_rsp_len},
      {"MinTxPower", adapter_info[0]->supported_capabilities->min_tx_power},
      {"MaxTxPower", adapter_info[0]->supported_capabilities->max_tx_power}};
  EXPECT_EQ(adapter_capabilities_info,
            advertising_properties->supported_capabilities.value());

  const auto& device_info = adapter_info[0]->connected_devices.value()[0];
  EXPECT_EQ(device_info->address, device_properties->address.value());
  EXPECT_EQ(device_info->name, device_properties->name.value());
  EXPECT_EQ(device_info->type,
            mock_bluez_manager->GetDeviceType(device_properties->type.value()));
  EXPECT_EQ(device_info->appearance->value,
            device_properties->appearance.value());
  EXPECT_EQ(device_info->modalias, device_properties->modalias.value());
  EXPECT_EQ(device_info->rssi->value, device_properties->rssi.value());
  EXPECT_EQ(device_info->mtu->value, device_properties->mtu.value());
  EXPECT_EQ(device_info->uuids, device_properties->uuids.value());
  EXPECT_EQ(device_info->battery_percentage->value,
            battery_properties->percentage.value());
}

// Test that getting no adapter and device objects is handled gracefully.
TEST_F(BluetoothUtilsTest, NoObjects) {
  auto mock_bluez_manager = std::make_unique<MockBluezInfoManager>();
  auto bluetooth_result =
      bluetooth_fetcher()->FetchBluetoothInfo(std::move(mock_bluez_manager));
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 0);
}

// Test that the number of connected devices is counted correctly.
TEST_F(BluetoothUtilsTest, NumConnectedDevices) {
  const auto adapter_properties = GetAdapterProperties();
  const auto device_properties = GetDeviceProperties();
  SetMockAdapterProxyCall(adapter_properties);
  SetMockDeviceProxyCall(device_properties, 2);

  auto mock_bluez_manager = std::make_unique<MockBluezInfoManager>();
  EXPECT_CALL(*mock_bluez_manager, adapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          mock_adapter_proxy()}));
  EXPECT_CALL(*mock_bluez_manager, devices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{
          mock_device_proxy(), mock_device_proxy()}));
  auto bluetooth_result =
      bluetooth_fetcher()->FetchBluetoothInfo(std::move(mock_bluez_manager));

  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  ASSERT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 2);
  ASSERT_TRUE(adapter_info[0]->connected_devices.has_value());
  EXPECT_EQ(adapter_info[0]->connected_devices.value().size(), 2);
}

// Test that a disconnected device is not counted as a connected device.
TEST_F(BluetoothUtilsTest, DisconnectedDevice) {
  const auto adapter_properties = GetAdapterProperties();
  const auto device_properties = GetDeviceProperties();
  SetMockAdapterProxyCall(adapter_properties);
  // Set as disconnected device.
  EXPECT_CALL(*mock_device_proxy(), connected()).WillOnce(Return(false));

  auto mock_bluez_manager = std::make_unique<MockBluezInfoManager>();
  EXPECT_CALL(*mock_bluez_manager, adapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          mock_adapter_proxy()}));
  EXPECT_CALL(*mock_bluez_manager, devices())
      .WillOnce(Return(std::vector<org::bluez::Device1ProxyInterface*>{
          mock_device_proxy()}));
  auto bluetooth_result =
      bluetooth_fetcher()->FetchBluetoothInfo(std::move(mock_bluez_manager));
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  ASSERT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 0);
  ASSERT_FALSE(adapter_info[0]->connected_devices.has_value());
}

}  // namespace
}  // namespace diagnostics
