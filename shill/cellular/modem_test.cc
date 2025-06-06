// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <net/if.h>
#include <sys/ioctl.h>

#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/net-base/mac_address.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ModemManager/ModemManager.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_capability_3gpp.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;
using testing::Test;

namespace shill {

namespace {

const int kTestInterfaceIndex = 5;
const char kTtyName[] = "ttyUSB0";
const char kLinkName[] = "usb0";
const char kService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kPath("/org/freedesktop/ModemManager1/Modem/0");
constexpr net_base::MacAddress kMacAddress(0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5);

enum ModemTestLayout {
  kModemTestLayoutControlOnly,     // TTY-only serial device that requires PPP
  kModemTestLayoutControlAndData,  // TTY+NET device
};

}  // namespace

class TestDeviceInfo : public MockDeviceInfo {
 public:
  explicit TestDeviceInfo(Manager* manager) : MockDeviceInfo(manager) {}
  TestDeviceInfo(const TestDeviceInfo&) = delete;
  TestDeviceInfo& operator=(const TestDeviceInfo&) = delete;

  // DeviceInfo override:
  void RegisterDevice(const DeviceRefPtr& device) override {
    if (device->technology() == Technology::kCellular) {
      cellular_ = static_cast<Cellular*>(device.get());
    }
  }
  DeviceRefPtr GetDevice(int interface_index) const override {
    if (cellular_ && interface_index == cellular_->interface_index()) {
      return cellular_;
    }
    return nullptr;
  }

 private:
  CellularRefPtr cellular_;
};

class ModemTest : public Test {
 public:
  ModemTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        modem_info_(&control_, &manager_),
        device_info_(&manager_),
        modem_(new Modem(kService, kPath, &device_info_)) {}

  void SetUp() override {
    EXPECT_CALL(device_info_, GetIndex(kLinkName))
        .WillRepeatedly(Return(kTestInterfaceIndex));

    EXPECT_CALL(manager_, device_info()).WillRepeatedly(Return(&device_info_));
    EXPECT_CALL(manager_, modem_info()).WillRepeatedly(Return(&modem_info_));
  }

  void TearDown() override { modem_.reset(); }

 protected:
  void SetDeviceInfoExpectations() {
    EXPECT_CALL(device_info_, IsDeviceBlocked(kLinkName))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(device_info_, GetByteCounts(kTestInterfaceIndex, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(device_info_, RegisterDevice(_)).Times(AnyNumber());
  }

  InterfaceToProperties GetInterfaceProperties(ModemTestLayout layout) {
    InterfaceToProperties properties;
    KeyValueStore modem_properties;
    modem_properties.Set<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED,
                                   MM_MODEM_LOCK_NONE);
    std::vector<std::tuple<std::string, uint32_t>> ports;
    std::tuple<std::string, uint32_t> tuple =
        std::make_tuple(kTtyName, MM_MODEM_PORT_TYPE_AT);
    ports.push_back(tuple);
    if (layout == kModemTestLayoutControlAndData) {
      tuple = std::make_tuple(kLinkName, MM_MODEM_PORT_TYPE_NET);
      ports.push_back(tuple);
    }
    modem_properties.SetVariant(MM_MODEM_PROPERTY_PORTS, brillo::Any(ports));
    modem_properties.Set<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES,
                                   MM_MODEM_CAPABILITY_LTE);
    properties[MM_DBUS_INTERFACE_MODEM] = modem_properties;

    return properties;
  }

  void CreateDevice(const InterfaceToProperties& properties) {
    modem_->CreateDevice(properties);
  }

  std::optional<std::pair<int, net_base::MacAddress>>
  GetLinkDetailsFromDeviceInfo() {
    return modem_->GetLinkDetailsFromDeviceInfo();
  }

  EventDispatcherForTest dispatcher_;
  NiceMock<MockControl> control_;
  MockMetrics metrics_;
  NiceMock<MockManager> manager_;
  MockModemInfo modem_info_;
  TestDeviceInfo device_info_;
  std::unique_ptr<Modem> modem_;
};

MATCHER_P2(HasPropertyWithValueU32, key, value, "") {
  return arg.template Contains<uint32_t>(key) &&
         value == arg.template Get<uint32_t>(key);
}

TEST_F(ModemTest, PendingDevicePropertiesAndCreate) {
  ASSERT_FALSE(modem_->interface_index_for_testing().has_value());

  SetDeviceInfoExpectations();

  // The first time we call CreateDevice, expect GetMacAddress to fail.
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex))
      .WillOnce(Return(std::nullopt));
  CreateDevice(GetInterfaceProperties(kModemTestLayoutControlAndData));
  EXPECT_TRUE(modem_->has_pending_device_info_for_testing());

  // When OnDeviceInfoAvailable gets called, CreateDeviceFromModemProperties
  // will get called. GetMacAddress is now expected to succeed, so a device will
  // get created.
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex))
      .WillOnce(Return(kMacAddress));
  modem_->OnDeviceInfoAvailable(kLinkName);
  EXPECT_FALSE(modem_->has_pending_device_info_for_testing());
  ASSERT_TRUE(modem_->interface_index_for_testing().has_value());
  DeviceRefPtr device =
      device_info_.GetDevice(Modem::kCellularDefaultInterfaceIndex);
  ASSERT_TRUE(device);
  EXPECT_EQ(kMacAddress, device->mac_address());
}

TEST_F(ModemTest, EarlyDeviceProperties) {
  // OnDeviceInfoAvailable called before CreateDeviceFromModemProperties does
  // nothing
  modem_->OnDeviceInfoAvailable(kLinkName);
  EXPECT_FALSE(modem_->interface_index_for_testing().has_value());
}

TEST_F(ModemTest, CreateDeviceEarlyFailures) {
  InterfaceToProperties properties;

  // No modem interface properties:  no device created
  CreateDevice(properties);
  EXPECT_FALSE(modem_->interface_index_for_testing().has_value());

  properties = GetInterfaceProperties(kModemTestLayoutControlAndData);

  // Link name, but no ifindex: no device created
  EXPECT_CALL(device_info_, GetIndex(StrEq(kLinkName))).WillOnce(Return(-1));
  CreateDevice(properties);
  EXPECT_FALSE(modem_->interface_index_for_testing().has_value());

  // The params are good, but the device is blocked.
  EXPECT_CALL(device_info_, GetIndex(StrEq(kLinkName)))
      .WillOnce(Return(kTestInterfaceIndex));
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex))
      .WillOnce(Return(kMacAddress));
  EXPECT_CALL(device_info_, IsDeviceBlocked(kLinkName))
      .WillRepeatedly(Return(true));
  CreateDevice(properties);
  ASSERT_TRUE(modem_->interface_index_for_testing().has_value());
  EXPECT_FALSE(
      device_info_.GetDevice(modem_->interface_index_for_testing().value()));

  // No link name: see CreateDevicePPP.
}

TEST_F(ModemTest, CreateDevicePPP) {
  // MM_MODEM_PROPERTY_PORTS has a single TTY, so GetLinkName will fail. Modem
  // will assume a PPP dongle and CreateDeviceFromModemProperties will succeed.
  EXPECT_CALL(device_info_, IsDeviceBlocked(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(device_info_, GetByteCounts(_, _, _))
      .WillRepeatedly(Return(true));
  CreateDevice(GetInterfaceProperties(kModemTestLayoutControlOnly));
  ASSERT_TRUE(modem_->interface_index_for_testing().has_value());
  int interface_index = modem_->interface_index_for_testing().value();
  EXPECT_EQ(interface_index, Modem::kFakeDevInterfaceIndex);
  DeviceRefPtr device =
      device_info_.GetDevice(Modem::kCellularDefaultInterfaceIndex);
  ASSERT_TRUE(device);
  EXPECT_EQ(device->mac_address(), Modem::kFakeDevAddress);
}

TEST_F(ModemTest, GetLinkDetailsFromDeviceInfo) {
  EXPECT_CALL(device_info_, GetMacAddress(_))
      .Times(AnyNumber())
      .WillRepeatedly(Return(std::nullopt));

  EXPECT_CALL(device_info_, GetIndex(_)).WillOnce(Return(-1));
  EXPECT_FALSE(GetLinkDetailsFromDeviceInfo().has_value());

  EXPECT_CALL(device_info_, GetIndex(_)).WillOnce(Return(-2));
  EXPECT_FALSE(GetLinkDetailsFromDeviceInfo().has_value());

  EXPECT_CALL(device_info_, GetIndex(_)).WillOnce(Return(1));
  EXPECT_CALL(device_info_, GetMacAddress(1)).WillOnce(Return(std::nullopt));
  EXPECT_FALSE(GetLinkDetailsFromDeviceInfo().has_value());

  EXPECT_CALL(device_info_, GetIndex(_)).WillOnce(Return(2));
  EXPECT_CALL(device_info_, GetMacAddress(2)).WillOnce(Return(kMacAddress));
  EXPECT_EQ(*GetLinkDetailsFromDeviceInfo(), std::make_pair(2, kMacAddress));
}

TEST_F(ModemTest, Create3gppDevice) {
  SetDeviceInfoExpectations();
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex))
      .WillOnce(Return(kMacAddress));

  InterfaceToProperties properties =
      GetInterfaceProperties(kModemTestLayoutControlAndData);

  KeyValueStore modem3gpp_properties;
  modem3gpp_properties.Set<uint32_t>(
      MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE,
      MM_MODEM_3GPP_REGISTRATION_STATE_HOME);
  properties[MM_DBUS_INTERFACE_MODEM_MODEM3GPP] = modem3gpp_properties;

  modem_->CreateDevice(properties);
  ASSERT_TRUE(modem_->interface_index_for_testing().has_value());
  DeviceRefPtr device =
      device_info_.GetDevice(Modem::kCellularDefaultInterfaceIndex);
  ASSERT_TRUE(device);
  Cellular* cellular = static_cast<Cellular*>(device.get());
  CellularCapability3gpp* capability = cellular->capability_for_testing();
  ASSERT_TRUE(capability);
  EXPECT_TRUE(capability->IsRegistered());
}

}  // namespace shill
