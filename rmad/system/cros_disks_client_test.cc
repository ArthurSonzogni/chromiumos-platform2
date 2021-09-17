// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <brillo/variant_dictionary.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/cros-disks/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/system/cros_disks_client_impl.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class CrosDisksClientTest : public testing::Test {
 public:
  CrosDisksClientTest()
      : mock_bus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        mock_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            mock_bus_.get(),
            cros_disks::kCrosDisksServiceName,
            dbus::ObjectPath(cros_disks::kCrosDisksServicePath))) {}
  ~CrosDisksClientTest() override = default;

  void SetUp() override {
    EXPECT_CALL(
        *mock_bus_,
        GetObjectProxy(cros_disks::kCrosDisksServiceName,
                       dbus::ObjectPath(cros_disks::kCrosDisksServicePath)))
        .WillOnce(Return(mock_object_proxy_.get()));
    cros_disks_client_ = std::make_unique<CrosDisksClientImpl>(mock_bus_);
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  std::unique_ptr<CrosDisksClientImpl> cros_disks_client_;
};

TEST_F(CrosDisksClientTest, EnumerateDevices_Success) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> cros_disks_response =
            dbus::Response::CreateEmpty();
        std::vector<std::string> devices = {"device1", "device2"};
        dbus::MessageWriter writer(cros_disks_response.get());
        writer.AppendArrayOfStrings(devices);
        return cros_disks_response;
      });

  std::vector<std::string> devices;
  EXPECT_TRUE(cros_disks_client_->EnumerateDevices(&devices));
  EXPECT_EQ(devices.size(), 2);
  EXPECT_EQ(devices[0], "device1");
  EXPECT_EQ(devices[1], "device2");
}

TEST_F(CrosDisksClientTest, EnumerateDevices_EmptyResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });

  std::vector<std::string> devices;
  EXPECT_FALSE(cros_disks_client_->EnumerateDevices(&devices));
}

TEST_F(CrosDisksClientTest, EnumerateDevices_NoResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  std::vector<std::string> devices;
  EXPECT_FALSE(cros_disks_client_->EnumerateDevices(&devices));
}

TEST_F(CrosDisksClientTest, GetDeviceProperties_Success) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> cros_disks_response =
            dbus::Response::CreateEmpty();
        brillo::VariantDictionary properties;
        properties["DeviceFile"] = "device_file";
        properties["DeviceIsOnRemovableDevice"] = true;
        properties["IsAutoMountable"] = true;
        dbus::MessageWriter writer(cros_disks_response.get());
        brillo::dbus_utils::AppendValueToWriter(&writer, properties);
        return cros_disks_response;
      });

  DeviceProperties device_properties;
  EXPECT_TRUE(cros_disks_client_->GetDeviceProperties("", &device_properties));
  EXPECT_EQ(device_properties.device_file, "device_file");
  EXPECT_TRUE(device_properties.is_on_removable_device);
  EXPECT_TRUE(device_properties.is_auto_mountable);
}

TEST_F(CrosDisksClientTest, GetDeviceProperties_EmptyResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });

  DeviceProperties device_properties;
  EXPECT_FALSE(cros_disks_client_->GetDeviceProperties("", &device_properties));
}

TEST_F(CrosDisksClientTest, GetDeviceProperties_NoResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  DeviceProperties device_properties;
  EXPECT_FALSE(cros_disks_client_->GetDeviceProperties("", &device_properties));
}

TEST_F(CrosDisksClientTest, AddMountCompletedHandler_MountSuccess) {
  dbus::Signal signal(cros_disks::kCrosDisksServiceName,
                      cros_disks::kMountCompleted);
  EXPECT_CALL(*mock_object_proxy_, DoConnectToSignal(_, _, _, _))
      .WillOnce(
          [](const std::string& interface, const std::string& signal,
             dbus::ObjectProxy::SignalCallback signal_callback,
             dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
            dbus::Signal mock_signal(interface, signal);
            dbus::MessageWriter writer(&mock_signal);
            writer.AppendUint32(cros_disks::MOUNT_ERROR_NONE);
            writer.AppendString("source");
            writer.AppendUint32(cros_disks::MOUNT_SOURCE_REMOVABLE_DEVICE);
            writer.AppendString("mount_path");

            signal_callback.Run(&mock_signal);
            std::move(*on_connected_callback).Run("", "", true);
          });

  auto callback = base::BindRepeating([](const MountEntry& entry) {
    EXPECT_TRUE(entry.success);
    EXPECT_EQ(entry.source, "source");
    EXPECT_EQ(entry.mount_path, "mount_path");
  });
  cros_disks_client_->AddMountCompletedHandler(callback);
}

TEST_F(CrosDisksClientTest, AddMountCompletedHandler_MountFailed) {
  dbus::Signal signal(cros_disks::kCrosDisksServiceName,
                      cros_disks::kMountCompleted);
  EXPECT_CALL(*mock_object_proxy_, DoConnectToSignal(_, _, _, _))
      .WillOnce(
          [](const std::string& interface, const std::string& signal,
             dbus::ObjectProxy::SignalCallback signal_callback,
             dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
            dbus::Signal mock_signal(interface, signal);
            dbus::MessageWriter writer(&mock_signal);
            writer.AppendUint32(cros_disks::MOUNT_ERROR_UNKNOWN);

            signal_callback.Run(&mock_signal);
            std::move(*on_connected_callback).Run("", "", false);
          });

  auto callback = base::BindRepeating(
      [](const MountEntry& entry) { EXPECT_FALSE(entry.success); });
  cros_disks_client_->AddMountCompletedHandler(callback);
}

TEST_F(CrosDisksClientTest, Mount_NoResponse) {
  // `Mount` doesn't return anything.
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  cros_disks_client_->Mount("source", "fs_type", {});
}

TEST_F(CrosDisksClientTest, Unmount_Success) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> cros_disks_response =
            dbus::Response::CreateEmpty();
        uint32_t result = 3;
        dbus::MessageWriter writer(cros_disks_response.get());
        writer.AppendUint32(result);
        return cros_disks_response;
      });

  uint32_t result;
  EXPECT_TRUE(cros_disks_client_->Unmount("", {}, &result));
  EXPECT_EQ(result, 3);
}

TEST_F(CrosDisksClientTest, Unmount_EmptyResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });

  uint32_t result;
  EXPECT_FALSE(cros_disks_client_->Unmount("", {}, &result));
}

TEST_F(CrosDisksClientTest, Unmount_NoResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  uint32_t result;
  EXPECT_FALSE(cros_disks_client_->Unmount("", {}, &result));
}

}  // namespace rmad
