// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

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

TEST_F(SaneClientTest, SaneClientSetsStatusOnSuccess) {
  std::unique_ptr<LibsaneWrapper> libsane;
  std::unique_ptr<SaneClient> client;
  std::unique_ptr<SaneDevice> device;
  SANE_Status status;
  brillo::ErrorPtr error;

  libsane = LibsaneWrapperImpl::Create();
  client = SaneClientImpl::Create(libsane.get());
  device = client->ConnectToDevice(&error, &status, "test");

  EXPECT_NE(device, nullptr);
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(status, SANE_STATUS_GOOD);
}

TEST_F(SaneClientTest, SaneClientSetsStatusOnBusy) {
  std::unique_ptr<LibsaneWrapper> libsane;
  std::unique_ptr<SaneClient> client;
  std::unique_ptr<SaneDevice> device;
  std::unique_ptr<SaneDevice> duplicate_device;
  SANE_Status status;
  brillo::ErrorPtr error;

  libsane = LibsaneWrapperImpl::Create();
  client = SaneClientImpl::Create(libsane.get());
  device = client->ConnectToDevice(&error, &status, "test");
  duplicate_device = client->ConnectToDevice(&error, &status, "test");

  EXPECT_NE(device, nullptr);
  EXPECT_EQ(duplicate_device, nullptr);
  EXPECT_NE(error, nullptr);
  EXPECT_EQ(status, SANE_STATUS_DEVICE_BUSY);
}

class FakeSaneWrapper : public LibsaneWrapperFake {
 public:
  void SetDeviceList(const SANE_Device** device_list) {
    device_list_ = device_list;
  }

  SANE_Status sane_get_devices(const SANE_Device*** device_list,
                               SANE_Bool local_only) override {
    if (!device_list_) {
      return SANE_STATUS_IO_ERROR;
    }
    *device_list = device_list_;
    return SANE_STATUS_GOOD;
  }

 private:
  const SANE_Device** device_list_ = nullptr;
};

// Verifies that ConnectToDevice() rejects undiscovered network/URI SANE device
// strings that target loopback, unspecified, DNS hostnames, custom Epson ports,
// or arbitrary network backends (b/524240148).
TEST_F(SaneClientTest, SaneClientRejectsUndiscoveredDevices) {
  FakeSaneWrapper libsane;
  const char* kInvalidDevices[] = {
      "airscan:escl:poc:http://127.0.0.1:34653/eSCL/",
      "airscan:escl:poc:http://localhost:34653/eSCL/",
      "airscan:escl:poc:http://[::1]:34653/eSCL/",
      "airscan:escl:poc:http://[::ffff:127.0.0.1]:34653/eSCL/",
      "airscan:escl:poc:http://0.0.0.0:34653/eSCL/",
      "epsonds:net:127.0.0.1:34653",
      "epson2:net:127.0.0.1:34653",
      "epsonds:net:127.0.0.1",
      "epson2:net:192.168.1.50:34653",
      "net:127.0.0.1:test",
      "escl:http://127.0.0.1:34653/eSCL/",
      "xerox_mfp:tcp 127.0.0.1 34653",
  };
  for (const char* dev_name : kInvalidDevices) {
    libsane.CreateScanner(dev_name);
  }
  std::unique_ptr<SaneClient> client = SaneClientImpl::Create(&libsane);

  for (const char* dev_name : kInvalidDevices) {
    SANE_Status status = SANE_STATUS_GOOD;
    brillo::ErrorPtr error;
    std::unique_ptr<SaneDevice> device =
        client->ConnectToDevice(&error, &status, dev_name);
    EXPECT_EQ(device, nullptr) << "Expected rejection for " << dev_name;
    EXPECT_NE(error, nullptr) << "Expected error for " << dev_name;
    EXPECT_EQ(status, SANE_STATUS_INVAL)
        << "Unexpected status for " << dev_name;
  }
}

// Verifies that AirScan (eSCL/WSD) and Epson network scanners targeting valid
// remote IPv4/IPv6 addresses can be opened without a prior ListDevices() call,
// supporting Chrome mDNS discovery, airscan-discover, epson_probe, and daemon
// restarts after idle shutdown (b/584727512).
TEST_F(SaneClientTest, SaneClientAllowsRemoteNetworkScannersWithoutDiscovery) {
  FakeSaneWrapper libsane;
  const char* kRemoteNetworkScanners[] = {
      "airscan:escl:HP LaserJet MFP M234dw (09195A):"
      "https://100.107.108.192/eSCL/",
      "airscan:escl:Other:http://192.168.1.99:80/eSCL/",
      "airscan:wsd:Konica Minolta Bizhub 3622:http://192.168.0.15:443/eSCL/",
      "airscan:escl:IPv6Scanner:http://[2001:db8::1]:8080/eSCL/",
      "epson2:net:192.168.1.50",
      "epsonds:net:192.168.1.50",
  };
  for (const char* dev_name : kRemoteNetworkScanners) {
    libsane.CreateScanner(dev_name);
  }
  std::unique_ptr<SaneClient> client = SaneClientImpl::Create(&libsane);

  for (const char* dev_name : kRemoteNetworkScanners) {
    SANE_Status status = SANE_STATUS_INVAL;
    brillo::ErrorPtr error;
    std::unique_ptr<SaneDevice> device =
        client->ConnectToDevice(&error, &status, dev_name);
    EXPECT_NE(device, nullptr) << "Expected success for " << dev_name;
    EXPECT_EQ(status, SANE_STATUS_GOOD) << "Unexpected status for " << dev_name;
  }
}

// Verifies that other network backends (e.g. kodakaio) are rejected before
// ListDevices() and permitted once discovered by ListDevices(), while local USB
// backends (e.g. pixma) work across idle restarts without ListDevices().
TEST_F(SaneClientTest, SaneClientAllowsDiscoveredFakeDevices) {
  FakeSaneWrapper libsane;
  dev_.name = "kodakaio:net:192.168.1.60";
  dev_two_.name = "epson2:net:192.168.1.50";
  libsane.SetDeviceList(two_devices_);
  libsane.CreateScanner("kodakaio:net:192.168.1.60");
  libsane.CreateScanner("pixma:04A9176D_123456");
  libsane.CreateScanner("epsonds:net:192.168.1.50");

  std::unique_ptr<SaneClient> client = SaneClientImpl::Create(&libsane);

  // Before ListDevices(), opening an undiscovered network backend must fail,
  // while local USB backends are allowed across idle restarts.
  SANE_Status status = SANE_STATUS_GOOD;
  brillo::ErrorPtr error;
  EXPECT_EQ(
      client->ConnectToDevice(&error, &status, "kodakaio:net:192.168.1.60"),
      nullptr);
  EXPECT_EQ(status, SANE_STATUS_INVAL);

  error.reset();
  std::unique_ptr<SaneDevice> pixma_device =
      client->ConnectToDevice(&error, &status, "pixma:04A9176D_123456");
  EXPECT_NE(pixma_device, nullptr);
  EXPECT_EQ(status, SANE_STATUS_GOOD);
  pixma_device.reset();

  // Discover devices via ListDevices().
  error.reset();
  std::optional<std::vector<ScannerInfo>> scanners =
      client->ListDevices(&error, false);
  ASSERT_TRUE(scanners.has_value());
  EXPECT_EQ(scanners->size(), 2);

  // Discovered network scanner can now be opened.
  error.reset();
  std::unique_ptr<SaneDevice> kodak_device =
      client->ConnectToDevice(&error, &status, "kodakaio:net:192.168.1.60");
  EXPECT_NE(kodak_device, nullptr);
  EXPECT_EQ(status, SANE_STATUS_GOOD);

  // Discovered "epson2:" scanner also allows probing via "epsonds:".
  error.reset();
  std::unique_ptr<SaneDevice> epsonds_device =
      client->ConnectToDevice(&error, &status, "epsonds:net:192.168.1.50");
  EXPECT_NE(epsonds_device, nullptr);
  EXPECT_EQ(status, SANE_STATUS_GOOD);
}

// Verifies that caller-supplied `airscan:...:unix://` device strings are
// rejected regardless of scheme casing (only `ippusb:` strings rewritten
// internally by BackendForDevice() may use `:unix://`).
TEST_F(SaneClientTest, SaneClientRejectsDirectUnixSocketDevices) {
  FakeSaneWrapper libsane;
  std::unique_ptr<SaneClient> client = SaneClientImpl::Create(&libsane);

  for (const char* dev_name : {
           "airscan:escl:Test:unix:///run/ippusb/1234-5678.sock/eSCL/",
           "airscan:escl:Test:UNIX:///run/ippusb/1234-5678.sock/eSCL/",
           "airscan:escl:Test:Unix:///run/ippusb/1234-5678.sock/eSCL/",
       }) {
    SANE_Status status = SANE_STATUS_GOOD;
    brillo::ErrorPtr error;
    EXPECT_EQ(client->ConnectToDevice(&error, &status, dev_name), nullptr);
    EXPECT_NE(error, nullptr);
    EXPECT_EQ(status, SANE_STATUS_INVAL);
  }
}

}  // namespace lorgnette
