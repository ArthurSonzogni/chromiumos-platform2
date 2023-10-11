// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher_floss.h"

#include <memory>
#include <string>
#include <vector>

#include <base/test/gmock_callback_support.h>
#include <base/test/test_future.h>
#include <base/test/task_environment.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/system/mock_floss_controller.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxy-mocks.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;

constexpr int32_t kDefaultHciInterface = 0;
const dbus::ObjectPath kDefaultAdapterPath{
    "/org/chromium/bluetooth/hci0/adapter"};
const dbus::ObjectPath kDefaultAdapterQAPath{"/org/chromium/bluetooth/hci0/qa"};
const dbus::ObjectPath kDefaultAdminPath{"/org/chromium/bluetooth/hci0/admin"};
// Test data of UUID bytes and corresponding string.
const std::vector<uint8_t> kTestUuidBytes = {0x00, 0x00, 0x11, 0x0a, 0x00, 0x00,
                                             0x10, 0x00, 0x80, 0x00, 0x00, 0x80,
                                             0x5f, 0x9b, 0x34, 0xfb};
constexpr char kTestUuidString[] = "0000110a-0000-1000-8000-00805f9b34fb";

class BluetoothFetcherFlossTest : public ::testing::Test {
 protected:
  BluetoothFetcherFlossTest() = default;
  BluetoothFetcherFlossTest(const BluetoothFetcherFlossTest&) = delete;
  BluetoothFetcherFlossTest& operator=(const BluetoothFetcherFlossTest&) =
      delete;
  ~BluetoothFetcherFlossTest() = default;

  void SetUp() override {
    EXPECT_CALL(*mock_floss_controller(), GetManager())
        .WillRepeatedly(Return(&mock_manager_proxy_));
  }

  MockFlossController* mock_floss_controller() {
    return mock_context_.mock_floss_controller();
  }

  mojom::BluetoothResultPtr FetchBluetoothInfoSync() {
    base::test::TestFuture<mojom::BluetoothResultPtr> future;
    FetchBluetoothInfoFromFloss(&mock_context_, future.GetCallback());
    return future.Take();
  }

  // Set the default adapter in available adapters.
  void SetupGetAvailableAdaptersCall(bool enabled = true) {
    const brillo::VariantDictionary default_adapter_info = {
        {"enabled", enabled}, {"hci_interface", kDefaultHciInterface}};

    EXPECT_CALL(mock_manager_proxy_, GetAvailableAdaptersAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(
            /*adapters=*/std::vector<brillo::VariantDictionary>{
                default_adapter_info}));
  }

  // Get the adapter with HCI interface 0.
  void SetupGetAdaptersCall() {
    EXPECT_CALL(*mock_floss_controller(), GetAdapters())
        .WillOnce(Return(
            std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{
                &mock_adapter_proxy_}));
    EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
        .WillOnce(ReturnRef(kDefaultAdapterPath));
  }

  // Get the adapter QA with HCI interface 0.
  void SetupGetAdapterQAsCall() {
    EXPECT_CALL(*mock_floss_controller(), GetAdapterQAs())
        .WillOnce(Return(
            std::vector<org::chromium::bluetooth::BluetoothQAProxyInterface*>{
                &mock_adapter_qa_proxy_}));
    EXPECT_CALL(mock_adapter_qa_proxy_, GetObjectPath)
        .WillOnce(ReturnRef(kDefaultAdapterQAPath));
  }

  // Get the admin with HCI interface 0.
  void SetupGetAdminsCall() {
    EXPECT_CALL(*mock_floss_controller(), GetAdmins())
        .WillOnce(
            Return(std::vector<
                   org::chromium::bluetooth::BluetoothAdminProxyInterface*>{
                &mock_admin_proxy_}));
    EXPECT_CALL(mock_admin_proxy_, GetObjectPath)
        .WillOnce(ReturnRef(kDefaultAdminPath));
  }

  void SetupFetchAdapterInfoCall(
      const std::vector<brillo::VariantDictionary>& connected_devices = {}) {
    EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
        .WillOnce(
            base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
    EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
    EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
    EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
    EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(
            /*services=*/std::vector<std::vector<uint8_t>>{kTestUuidBytes}));
    EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(connected_devices));
  }

  MockContext mock_context_;
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
  StrictMock<org::chromium::bluetooth::BluetoothQAProxyMock>
      mock_adapter_qa_proxy_;
  StrictMock<org::chromium::bluetooth::BluetoothAdminProxyMock>
      mock_admin_proxy_;

 private:
  base::test::TaskEnvironment task_environment_;
};

// Test that Bluetooth info can be fetched successfully.
TEST_F(BluetoothFetcherFlossTest, DefaultAdapterEnabled) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall();

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->name, "Chromebook_C20B");
  EXPECT_EQ(adapter_info[0]->address, "C4:23:60:59:2B:75");
  EXPECT_TRUE(adapter_info[0]->powered);
  EXPECT_TRUE(adapter_info[0]->discoverable);
  EXPECT_TRUE(adapter_info[0]->discovering);
  EXPECT_EQ(adapter_info[0]->uuids.value().size(), 1);
  EXPECT_EQ(adapter_info[0]->uuids.value()[0], kTestUuidString);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 0);
  ASSERT_TRUE(adapter_info[0]->connected_devices.has_value());
  EXPECT_EQ(adapter_info[0]->connected_devices.value().size(), 0);
}

// Test that Bluetooth info can be fetched successfully when the powered is off.
TEST_F(BluetoothFetcherFlossTest, DefaultAdapterDisabled) {
  InSequence s;
  SetupGetAvailableAdaptersCall(/*enabled=*/false);

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->name, "hci0 (disabled)");
  EXPECT_EQ(adapter_info[0]->address, "");
  EXPECT_FALSE(adapter_info[0]->powered);
  EXPECT_FALSE(adapter_info[0]->discoverable);
  EXPECT_FALSE(adapter_info[0]->discovering);
  EXPECT_FALSE(adapter_info[0]->uuids.has_value());
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 0);
  ASSERT_TRUE(adapter_info[0]->connected_devices.has_value());
  EXPECT_EQ(adapter_info[0]->connected_devices.value().size(), 0);
}

// Test that the error of getting adapter address is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterAddressError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();

  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));
  EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*uuids=*/std::vector<std::vector<uint8_t>>{kTestUuidBytes}));
  EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*devices=*/std::vector<brillo::VariantDictionary>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get adapter address");
}

// Test that the error of getting adapter name is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterNameError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();

  EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
      .WillOnce(
          base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*uuids=*/std::vector<std::vector<uint8_t>>{kTestUuidBytes}));
  EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*devices=*/std::vector<brillo::VariantDictionary>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get adapter name");
}

// Test that the error of getting adapter discovering is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterDiscoveringError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();

  EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
      .WillOnce(
          base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
  EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));
  EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*uuids=*/std::vector<std::vector<uint8_t>>{kTestUuidBytes}));
  EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*devices=*/std::vector<brillo::VariantDictionary>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get adapter discovering");
}

// Test that the error of getting adapter discoverable is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterDiscoverableError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();

  EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
      .WillOnce(
          base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
  EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));
  EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*uuids=*/std::vector<std::vector<uint8_t>>{kTestUuidBytes}));
  EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*devices=*/std::vector<brillo::VariantDictionary>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get adapter discoverable");
}

// Test that the error of getting adapter UUIDs is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterUUIDsError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();

  EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
      .WillOnce(
          base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
  EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));
  EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*devices=*/std::vector<brillo::VariantDictionary>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get adapter UUIDs");
}

// Test that the error of parsing adapter UUIDs is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseAdapterUUIDsError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();

  EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
      .WillOnce(
          base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
  EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*uuids=*/std::vector<std::vector<uint8_t>>{/*invalid_uuid=*/{}}));
  EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*devices=*/std::vector<brillo::VariantDictionary>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to parse UUID from adapter UUIDs");
}

// Test that adapter modalias can be fetched successfully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterModalias) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall();

  SetupGetAdapterQAsCall();
  EXPECT_CALL(mock_adapter_qa_proxy_, GetModaliasAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*modalias=*/"bluetooth:v00E0pC405d0001"));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->modalias, "bluetooth:v00E0pC405d0001");
}

// Test that the error of getting adapter modalias is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterModaliasError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall();

  SetupGetAdapterQAsCall();
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_qa_proxy_, GetModaliasAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get adapter modalias");
}

// Test that adapter allowed services can be fetched successfully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterServiceAllowList) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall();

  SetupGetAdminsCall();
  EXPECT_CALL(mock_admin_proxy_, GetAllowedServicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*services=*/std::vector<std::vector<uint8_t>>{kTestUuidBytes}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->service_allow_list.value().size(), 1);
  EXPECT_EQ(adapter_info[0]->service_allow_list.value()[0], kTestUuidString);
}

// Test that the error of getting adapter allowed services is handled
// gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterServiceAllowListError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall();

  SetupGetAdminsCall();
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_admin_proxy_, GetAllowedServicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get adapter allowed services");
}

// Test that the error of parsing adapter allowed services is handled
// gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseAdapterAllowedServicesError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall();

  SetupGetAdminsCall();
  EXPECT_CALL(mock_admin_proxy_, GetAllowedServicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*services=*/std::vector<std::vector<uint8_t>>{/*invalid_uuid=*/{}}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to parse UUID from allowed services");
}

// Test that the error of getting connected devices is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetConnectedDevicesError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();

  EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
      .WillOnce(
          base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
  EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
  EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*uuids=*/std::vector<std::vector<uint8_t>>{kTestUuidBytes}));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get connected devices");
}

// Test that the error of parsing connected devices can be handled correctly.
TEST_F(BluetoothFetcherFlossTest, ParseConnectedDevicesError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  const std::vector<brillo::VariantDictionary> connected_devices = {
      {{"name", std::string("Test device")}, {"no_address", std::string("")}}};
  SetupFetchAdapterInfoCall(connected_devices);

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to parse connected devices");
}

// Test that connected devices info can be fetched successfully.
TEST_F(BluetoothFetcherFlossTest, ConnectedDevices) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  const std::vector<brillo::VariantDictionary> connected_devices = {
      {{"name", std::string("Test device")},
       {"address", std::string("70:88:6B:92:34:70")}}};
  SetupFetchAdapterInfoCall(connected_devices);

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 1);
  ASSERT_TRUE(adapter_info[0]->connected_devices.has_value());
  const auto& devices = adapter_info[0]->connected_devices.value();
  EXPECT_EQ(devices.size(), 1);
  EXPECT_EQ(devices[0]->name, "Test device");
  EXPECT_EQ(devices[0]->address, "70:88:6B:92:34:70");
}

// Test that the error of getting target adapter is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, MissingAvailableAdapter) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  EXPECT_CALL(*mock_floss_controller(), GetAdapters())
      .WillOnce(Return(
          std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get target adapter");
}

// Test that getting empty available adapters is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, NoAdapters) {
  InSequence s;
  EXPECT_CALL(mock_manager_proxy_, GetAvailableAdaptersAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*adapters=*/std::vector<brillo::VariantDictionary>{}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 0);
}

// Test that the error of getting available adapters can be handled correctly.
TEST_F(BluetoothFetcherFlossTest, GetAvailableAdaptersError) {
  InSequence s;
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, GetAvailableAdaptersAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error.get()));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get available adapters");
}

// Test that the error of parsing available adapters can be handled correctly.
TEST_F(BluetoothFetcherFlossTest, ParseAvailableAdaptersError) {
  InSequence s;
  const brillo::VariantDictionary wrong_adapter_info = {
      {"no_enabled", false}, {"hci_interface", kDefaultHciInterface}};

  EXPECT_CALL(mock_manager_proxy_, GetAvailableAdaptersAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(
          /*adapters=*/std::vector<brillo::VariantDictionary>{
              wrong_adapter_info}));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to parse available adapters");
}

// Test that the error of getting Bluetooth managers can be handled correctly.
TEST_F(BluetoothFetcherFlossTest, GetBluetoothManagerError) {
  InSequence s;
  EXPECT_CALL(*mock_floss_controller(), GetManager())
      .WillRepeatedly(Return(nullptr));

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Floss proxy is not ready");
}

}  // namespace
}  // namespace diagnostics
