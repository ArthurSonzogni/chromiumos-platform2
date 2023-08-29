// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include <base/containers/contains.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/constants.h"
#include "lorgnette/libsane_wrapper.h"
#include "lorgnette/libsane_wrapper_fake.h"
#include "lorgnette/libsane_wrapper_impl.h"
#include "lorgnette/manager.h"
#include "lorgnette/test_util.h"

using ::testing::ElementsAre;

namespace lorgnette {

class SaneClientTest : public testing::Test {
 protected:
  void SetUp() override {
    dev_ = CreateTestDevice();
    dev_two_ = CreateTestDevice();
  }

  static SANE_Device CreateTestDevice() {
    SANE_Device dev;
    dev.name = "Test Name";
    dev.vendor = "Test Vendor";
    dev.model = "Test Model";
    dev.type = "film scanner";

    return dev;
  }

  SANE_Device dev_;
  SANE_Device dev_two_;
  const SANE_Device* empty_devices_[1] = {NULL};
  const SANE_Device* one_device_[2] = {&dev_, NULL};
  const SANE_Device* two_devices_[3] = {&dev_, &dev_two_, NULL};
};

TEST_F(SaneClientTest, ScannerInfoFromDeviceListInvalidParameters) {
  EXPECT_FALSE(SaneClientImpl::DeviceListToScannerInfo(NULL).has_value());
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListNoDevices) {
  std::optional<std::vector<ScannerInfo>> info =
      SaneClientImpl::DeviceListToScannerInfo(empty_devices_);
  EXPECT_TRUE(info.has_value());
  EXPECT_EQ(info->size(), 0);
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListOneDevice) {
  std::optional<std::vector<ScannerInfo>> opt_info =
      SaneClientImpl::DeviceListToScannerInfo(one_device_);
  EXPECT_TRUE(opt_info.has_value());
  std::vector<ScannerInfo> info = opt_info.value();
  ASSERT_EQ(info.size(), 1);
  EXPECT_EQ(info[0].name(), dev_.name);
  EXPECT_EQ(info[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info[0].model(), dev_.model);
  EXPECT_EQ(info[0].type(), dev_.type);
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListNullFields) {
  dev_ = CreateTestDevice();
  dev_.name = NULL;
  std::optional<std::vector<ScannerInfo>> opt_info =
      SaneClientImpl::DeviceListToScannerInfo(one_device_);
  EXPECT_TRUE(opt_info.has_value());
  EXPECT_EQ(opt_info->size(), 0);

  dev_ = CreateTestDevice();
  dev_.vendor = NULL;
  opt_info = SaneClientImpl::DeviceListToScannerInfo(one_device_);
  EXPECT_TRUE(opt_info.has_value());
  std::vector<ScannerInfo> info = opt_info.value();
  ASSERT_EQ(info.size(), 1);
  EXPECT_EQ(info[0].name(), dev_.name);
  EXPECT_EQ(info[0].manufacturer(), "");
  EXPECT_EQ(info[0].model(), dev_.model);
  EXPECT_EQ(info[0].type(), dev_.type);

  dev_ = CreateTestDevice();
  dev_.model = NULL;
  opt_info = SaneClientImpl::DeviceListToScannerInfo(one_device_);
  EXPECT_TRUE(opt_info.has_value());
  info = opt_info.value();
  ASSERT_EQ(info.size(), 1);
  EXPECT_EQ(info[0].name(), dev_.name);
  EXPECT_EQ(info[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info[0].model(), "");
  EXPECT_EQ(info[0].type(), dev_.type);

  dev_ = CreateTestDevice();
  dev_.type = NULL;
  opt_info = SaneClientImpl::DeviceListToScannerInfo(one_device_);
  EXPECT_TRUE(opt_info.has_value());
  info = opt_info.value();
  ASSERT_EQ(info.size(), 1);
  EXPECT_EQ(info[0].name(), dev_.name);
  EXPECT_EQ(info[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info[0].model(), dev_.model);
  EXPECT_EQ(info[0].type(), "");
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListMultipleDevices) {
  std::optional<std::vector<ScannerInfo>> opt_info =
      SaneClientImpl::DeviceListToScannerInfo(two_devices_);
  EXPECT_FALSE(opt_info.has_value());

  dev_two_.name = "Test Device 2";
  dev_two_.vendor = "Test Vendor 2";
  opt_info = SaneClientImpl::DeviceListToScannerInfo(two_devices_);
  EXPECT_TRUE(opt_info.has_value());
  std::vector<ScannerInfo> info = opt_info.value();
  ASSERT_EQ(info.size(), 2);
  EXPECT_EQ(info[0].name(), dev_.name);
  EXPECT_EQ(info[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info[0].model(), dev_.model);
  EXPECT_EQ(info[0].type(), dev_.type);

  EXPECT_EQ(info[1].name(), dev_two_.name);
  EXPECT_EQ(info[1].manufacturer(), dev_two_.vendor);
  EXPECT_EQ(info[1].model(), dev_two_.model);
  EXPECT_EQ(info[1].type(), dev_two_.type);
}

}  // namespace lorgnette
