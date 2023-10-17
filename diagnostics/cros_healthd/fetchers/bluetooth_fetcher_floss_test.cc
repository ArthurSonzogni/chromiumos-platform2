// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher_floss.h"

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
const brillo::VariantDictionary kTestConnectedDevice = {
    {"name", std::string("Test device")},
    {"address", std::string("70:88:6B:92:34:70")}};
// Test data of UUID bytes and corresponding string.
const std::vector<uint8_t> kTestUuidBytes = {0x00, 0x00, 0x11, 0x0a, 0x00, 0x00,
                                             0x10, 0x00, 0x80, 0x00, 0x00, 0x80,
                                             0x5f, 0x9b, 0x34, 0xfb};
constexpr char kTestUuidString[] = "0000110a-0000-1000-8000-00805f9b34fb";
// Test data of vendor product info and corresponding modalias.
const brillo::VariantDictionary kTestDeviceVendorProductInfo = {
    {"vendor_id_src", uint8_t(0x1)},
    {"vendor_id", uint16_t(0x4C)},
    {"product_id", uint16_t(0x200F)},
    {"version", uint16_t(0xC12C)}};
constexpr char kTestDeviceModalias[] = "bluetooth:v004Cp200FdC12C";

// Default settings for fetching adapter info.
struct FetchAdapterInfoDetails {
  bool get_name_error = false;
  bool get_address_error = false;
  bool get_discovering_error = false;
  bool get_discoverable_error = false;
  bool get_uuids_error = false;
  bool get_connected_devices_error = false;
  std::vector<brillo::VariantDictionary> connected_devices = {};
  std::vector<std::vector<uint8_t>> uuids = {kTestUuidBytes};
};

// Default settings for fetching device info.
struct FetchDeviceInfoDetails {
  bool get_type_error = false;
  bool get_appearance_error = false;
  bool get_modalias_error = false;
  bool get_uuids_error = false;
  bool get_bluetooth_class_error = false;
  uint32_t type = 0;
  brillo::VariantDictionary vendor_product_info = kTestDeviceVendorProductInfo;
  std::vector<std::vector<uint8_t>> uuids = {kTestUuidBytes};
};

class BluetoothFetcherFlossTest : public ::testing::Test {
 protected:
  BluetoothFetcherFlossTest() = default;
  BluetoothFetcherFlossTest(const BluetoothFetcherFlossTest&) = delete;
  BluetoothFetcherFlossTest& operator=(const BluetoothFetcherFlossTest&) =
      delete;
  ~BluetoothFetcherFlossTest() = default;

  void SetUp() override {
    error_ = brillo::Error::Create(FROM_HERE, "", "", "");
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

  void SetupFetchAdapterInfoCall(FetchAdapterInfoDetails details = {}) {
    if (!details.get_address_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
          .WillOnce(
              base::test::RunOnceCallback<0>(/*address=*/"C4:23:60:59:2B:75"));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetAddressAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(error_.get()));
    }
    if (!details.get_name_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>(/*name=*/"Chromebook_C20B"));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetNameAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(error_.get()));
    }
    if (!details.get_discovering_error) {
      EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(error_.get()));
    }
    if (!details.get_discoverable_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>(/*discoverable=*/true));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetDiscoverableAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(error_.get()));
    }
    if (!details.get_uuids_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>(details.uuids));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetUuidsAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(error_.get()));
    }
    if (!details.get_connected_devices_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>(details.connected_devices));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetConnectedDevicesAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(error_.get()));
    }
  }

  void SetupFetchDeviceInfoCall(const brillo::VariantDictionary& device,
                                FetchDeviceInfoDetails details = {}) {
    if (!details.get_type_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetRemoteTypeAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(details.type));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetRemoteTypeAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
    }
    if (!details.get_appearance_error) {
      EXPECT_CALL(mock_adapter_proxy_,
                  GetRemoteAppearanceAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(/*appearance=*/2371));
    } else {
      EXPECT_CALL(mock_adapter_proxy_,
                  GetRemoteAppearanceAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
    }
    if (!details.get_modalias_error) {
      EXPECT_CALL(mock_adapter_proxy_,
                  GetRemoteVendorProductInfoAsync(device, _, _, _))
          .WillOnce(
              base::test::RunOnceCallback<1>(details.vendor_product_info));
    } else {
      EXPECT_CALL(mock_adapter_proxy_,
                  GetRemoteVendorProductInfoAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
    }
    if (!details.get_uuids_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetRemoteUuidsAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(details.uuids));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetRemoteUuidsAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
    }
    if (!details.get_bluetooth_class_error) {
      EXPECT_CALL(mock_adapter_proxy_, GetRemoteClassAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(/*bluetooth_class=*/236034));
    } else {
      EXPECT_CALL(mock_adapter_proxy_, GetRemoteClassAsync(device, _, _, _))
          .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
    }
  }

  MockContext mock_context_;
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
  StrictMock<org::chromium::bluetooth::BluetoothQAProxyMock>
      mock_adapter_qa_proxy_;
  StrictMock<org::chromium::bluetooth::BluetoothAdminProxyMock>
      mock_admin_proxy_;
  brillo::ErrorPtr error_;

 private:
  base::test::TaskEnvironment task_environment_;
};

// Test that Bluetooth info can be fetched successfully.
TEST_F(BluetoothFetcherFlossTest, DefaultAdapterEnabled) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall(
      /*details=*/{.connected_devices = {}, .uuids = {kTestUuidBytes}});

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
  SetupFetchAdapterInfoCall({.get_address_error = true});

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
  SetupFetchAdapterInfoCall({.get_name_error = true});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get adapter name");
}

// Test that the error of getting adapter discovering is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetAdapterDiscoveringError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.get_discovering_error = true});

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
  SetupFetchAdapterInfoCall({.get_discoverable_error = true});

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
  SetupFetchAdapterInfoCall({.get_uuids_error = true});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get adapter UUIDs");
}

// Test that the error of parsing adapter UUIDs is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseAdapterUUIDsError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.uuids = {/*invalid_uuid=*/{}}});

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
  EXPECT_CALL(mock_adapter_qa_proxy_, GetModaliasAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error_.get()));

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
  EXPECT_CALL(mock_admin_proxy_, GetAllowedServicesAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error_.get()));

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
  SetupFetchAdapterInfoCall({.get_connected_devices_error = true});

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
  const brillo::VariantDictionary connected_device = {
      {"name", std::string("Test device")}, {"no_address", std::string("")}};
  SetupFetchAdapterInfoCall({.connected_devices = {connected_device}});

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
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(kTestConnectedDevice,
                           /*details=*/{.type = 1, .uuids = {kTestUuidBytes}});

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
  EXPECT_EQ(devices[0]->type, mojom::BluetoothDeviceType::kBrEdr);
  EXPECT_EQ(devices[0]->appearance->value, 2371);
  EXPECT_EQ(devices[0]->modalias, kTestDeviceModalias);
  EXPECT_EQ(devices[0]->uuids.value().size(), 1);
  EXPECT_EQ(devices[0]->uuids.value()[0], kTestUuidString);
  EXPECT_EQ(devices[0]->bluetooth_class->value, 236034);
}

// Test that the error of getting device type is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetDeviceTypeError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(kTestConnectedDevice, {.get_type_error = true});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get device type");
}

// Test that the error of parsing device type is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseDeviceTypeError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  // The max value of device type enum is 3.
  SetupFetchDeviceInfoCall(kTestConnectedDevice, {.type = 4});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to parse device type");
}

// Test that the error of getting device appearance is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetDeviceAppearanceError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(kTestConnectedDevice,
                           {.get_appearance_error = true});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get device appearance");
}

// Test that the error of getting device modalias is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetDeviceModaliasError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(kTestConnectedDevice, {.get_modalias_error = true});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to get device modalias");
}

// Test that the error of parsing device modalias is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseDeviceModaliasError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(
      kTestConnectedDevice,
      {.vendor_product_info = {{"no_vendor_id_src", uint8_t(0x1)},
                               {"vendor_id", uint16_t(0x4C)},
                               {"no_product_id", uint16_t(0x200F)},
                               {"version", uint16_t(0xC12C)}}});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to parse device modalias");
}

// Test that the unknown vendor ID source is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseUnknownVendorIDSource) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  // The unknown value of vendor ID source is 0.
  SetupFetchDeviceInfoCall(
      kTestConnectedDevice,
      {.vendor_product_info = {{"vendor_id_src", uint8_t(0x0)},
                               {"vendor_id", uint16_t(0x4C)},
                               {"product_id", uint16_t(0x200F)},
                               {"version", uint16_t(0xC12C)}}});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 1);
  ASSERT_TRUE(adapter_info[0]->connected_devices.has_value());
  const auto& devices = adapter_info[0]->connected_devices.value();
  EXPECT_EQ(devices.size(), 1);
  EXPECT_EQ(devices[0]->modalias, std::nullopt);
}

// Test that the error of parsing vendor ID source is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseVendorIDSourceError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  // The max value of vendor ID source is 2.
  SetupFetchDeviceInfoCall(
      kTestConnectedDevice,
      {.vendor_product_info = {{"vendor_id_src", uint8_t(0x3)},
                               {"vendor_id", uint16_t(0x4C)},
                               {"product_id", uint16_t(0x200F)},
                               {"version", uint16_t(0xC12C)}}});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to parse vendor ID source");
}

// Test that the error of getting device UUIDs is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetDeviceUUIDsError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(kTestConnectedDevice, {.get_uuids_error = true});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get device UUIDs");
}

// Test that the error of parsing device UUIDs is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, ParseDeviceUUIDsError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(kTestConnectedDevice,
                           {.uuids = {/*invalid_uuid=*/{}}});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg,
            "Failed to parse UUID from device UUIDs");
}

// Test that the error of getting device class is handled gracefully.
TEST_F(BluetoothFetcherFlossTest, GetDeviceClassError) {
  InSequence s;
  SetupGetAvailableAdaptersCall();
  SetupGetAdaptersCall();
  SetupFetchAdapterInfoCall({.connected_devices = {kTestConnectedDevice}});
  SetupFetchDeviceInfoCall(kTestConnectedDevice,
                           {.get_bluetooth_class_error = true});

  auto bluetooth_result = FetchBluetoothInfoSync();
  ASSERT_TRUE(bluetooth_result->is_error());
  EXPECT_EQ(bluetooth_result->get_error()->msg, "Failed to get device class");
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
  EXPECT_CALL(mock_manager_proxy_, GetAvailableAdaptersAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error_.get()));

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
