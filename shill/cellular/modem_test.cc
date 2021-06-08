// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <tuple>
#include <utility>

#include <ModemManager/ModemManager.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_capability.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/dbus/dbus_properties_proxy.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/net/mock_rtnl_handler.h"
#include "shill/net/rtnl_handler.h"
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
const char kLinkName[] = "usb0";
const char kService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kPath("/org/freedesktop/ModemManager1/Modem/0");
const unsigned char kAddress[] = {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5};
const char kAddressAsString[] = "A0B1C2D3E4F5";

}  // namespace

class TestDeviceInfo : public MockDeviceInfo {
 public:
  explicit TestDeviceInfo(Manager* manager) : MockDeviceInfo(manager) {}
  TestDeviceInfo(const TestDeviceInfo&) = delete;
  TestDeviceInfo& operator=(const TestDeviceInfo&) = delete;

  // DeviceInfo override:
  CellularRefPtr GetCellularDevice(int interface_index,
                                   const std::string& mac_address,
                                   Modem* modem) override {
    CellularRefPtr cellular;
    if (use_mock_) {
      cellular = new NiceMock<MockCellular>(
          manager()->modem_info(), kLinkName, mac_address, interface_index,
          Cellular::kType3gpp, kService, kPath);
    } else {
      cellular = new Cellular(manager()->modem_info(), kLinkName, mac_address,
                              interface_index, modem->type(), modem->service(),
                              modem->path());
      cellular->CreateCapability(manager()->modem_info());
    }
    return cellular;
  }

  void set_use_mock(bool use_mock) { use_mock_ = use_mock; }

 private:
  bool use_mock_ = true;
};

class ModemTest : public Test {
 public:
  ModemTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        modem_info_(&control_, &manager_),
        device_info_(&manager_),
        modem_(new Modem(kService, kPath, &device_info_)) {
    modem_->set_rtnl_handler_for_testing(&rtnl_handler_);
  }

  void SetUp() {
    expected_address_ = ByteString(kAddress, base::size(kAddress));

    EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(kLinkName))
        .WillRepeatedly(Return(kTestInterfaceIndex));

    EXPECT_CALL(manager_, device_info()).WillRepeatedly(Return(&device_info_));
    EXPECT_CALL(manager_, modem_info()).WillRepeatedly(Return(&modem_info_));
  }

  void TearDown() { modem_.reset(); }

 protected:
  void SetUseRealCellular() { device_info_.set_use_mock(false); }

  void SetDeviceInfoExpectations() {
    EXPECT_CALL(device_info_, IsDeviceBlocked(kLinkName))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(device_info_, GetByteCounts(kTestInterfaceIndex, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(device_info_, RegisterDevice(_)).Times(AnyNumber());
  }

  InterfaceToProperties GetInterfaceProperties() {
    InterfaceToProperties properties;
    KeyValueStore modem_properties;
    modem_properties.Set<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED,
                                   MM_MODEM_LOCK_NONE);
    std::vector<std::tuple<std::string, uint32_t>> ports = {
        std::make_tuple(kLinkName, MM_MODEM_PORT_TYPE_NET)};
    modem_properties.SetVariant(MM_MODEM_PROPERTY_PORTS, brillo::Any(ports));
    properties[MM_DBUS_INTERFACE_MODEM] = modem_properties;

    return properties;
  }

  void CreateDeviceFromModemProperties(
      const InterfaceToProperties& properties) {
    modem_->CreateDeviceFromModemProperties(properties);
  }

  bool GetDeviceParams(std::string* mac_address, int* interface_index) {
    return modem_->GetDeviceParams(mac_address, interface_index);
  }

  EventDispatcherForTest dispatcher_;
  NiceMock<MockControl> control_;
  MockMetrics metrics_;
  NiceMock<MockManager> manager_;
  MockModemInfo modem_info_;
  TestDeviceInfo device_info_;
  std::unique_ptr<Modem> modem_;
  MockRTNLHandler rtnl_handler_;
  ByteString expected_address_;
};

MATCHER_P2(HasPropertyWithValueU32, key, value, "") {
  return arg.template Contains<uint32_t>(key) &&
         value == arg.template Get<uint32_t>(key);
}

TEST_F(ModemTest, PendingDevicePropertiesAndCreate) {
  ASSERT_FALSE(modem_->device_for_testing());

  SetDeviceInfoExpectations();

  // The first time we call CreateDeviceFromModemProperties, expect
  // GetMacAddress to fail.
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(Return(false));
  CreateDeviceFromModemProperties(GetInterfaceProperties());
  EXPECT_FALSE(modem_->device_for_testing());
  EXPECT_TRUE(modem_->has_pending_device_info_for_testing());

  // When OnDeviceInfoAvailable gets called, CreateDeviceFromModemProperties
  // will get called again. GetMacAddress is now expected to succeed, so a
  // device will get created.
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));
  modem_->OnDeviceInfoAvailable(kLinkName);
  EXPECT_TRUE(modem_->device_for_testing());
  EXPECT_EQ(base::ToLowerASCII(kAddressAsString),
            modem_->device_for_testing()->mac_address());
}

TEST_F(ModemTest, EarlyDeviceProperties) {
  // OnDeviceInfoAvailable called before CreateDeviceFromModemProperties does
  // nothing
  modem_->OnDeviceInfoAvailable(kLinkName);
  EXPECT_FALSE(modem_->device_for_testing());
}

TEST_F(ModemTest, CreateDeviceEarlyFailures) {
  InterfaceToProperties properties;

  // No modem interface properties:  no device created
  CreateDeviceFromModemProperties(properties);
  EXPECT_FALSE(modem_->device_for_testing());

  properties = GetInterfaceProperties();

  // Link name, but no ifindex: no device created
  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(StrEq(kLinkName)))
      .WillOnce(Return(-1));
  CreateDeviceFromModemProperties(properties);
  EXPECT_FALSE(modem_->device_for_testing());

  // The params are good, but the device is blocked.
  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(StrEq(kLinkName)))
      .WillOnce(Return(kTestInterfaceIndex));
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));
  EXPECT_CALL(device_info_, IsDeviceBlocked(kLinkName))
      .WillRepeatedly(Return(true));
  CreateDeviceFromModemProperties(properties);
  EXPECT_FALSE(modem_->device_for_testing());

  // No link name: see CreateDevicePPP.
}

TEST_F(ModemTest, CreateDevicePPP) {
  InterfaceToProperties properties;
  properties[MM_DBUS_INTERFACE_MODEM] = KeyValueStore();

  // MM_MODEM_PROPERTY_PORTS is unset, so GetLinkName will fail. Modem will
  // assume a PPP dongle and CreateDeviceFromModemProperties will succeed.
  EXPECT_CALL(device_info_, IsDeviceBlocked(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(device_info_, GetByteCounts(_, _, _))
      .WillRepeatedly(Return(true));
  CreateDeviceFromModemProperties(properties);
  EXPECT_TRUE(modem_->device_for_testing());
  EXPECT_EQ(modem_->device_for_testing()->interface_index(),
            Modem::kFakeDevInterfaceIndex);
  EXPECT_EQ(modem_->device_for_testing()->mac_address(),
            Modem::kFakeDevAddress);
}

TEST_F(ModemTest, GetDeviceParams) {
  std::string mac_address;
  int interface_index = 2;
  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(-1));
  EXPECT_CALL(device_info_, GetMacAddress(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(-1, interface_index);

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(-2));
  EXPECT_CALL(device_info_, GetMacAddress(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(-2, interface_index);

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(1));
  EXPECT_CALL(device_info_, GetMacAddress(_, _)).WillOnce(Return(false));
  EXPECT_FALSE(GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(1, interface_index);

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(2));
  EXPECT_CALL(device_info_, GetMacAddress(2, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));
  EXPECT_TRUE(GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(2, interface_index);
  EXPECT_EQ(kAddressAsString, mac_address);
}

TEST_F(ModemTest, CreateDevice) {
  SetUseRealCellular();
  SetDeviceInfoExpectations();
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));

  InterfaceToProperties properties = GetInterfaceProperties();

  KeyValueStore modem3gpp_properties;
  modem3gpp_properties.Set<uint32_t>(
      MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE,
      MM_MODEM_3GPP_REGISTRATION_STATE_HOME);
  properties[MM_DBUS_INTERFACE_MODEM_MODEM3GPP] = modem3gpp_properties;

  std::unique_ptr<DBusPropertiesProxy> dbus_properties_proxy =
      DBusPropertiesProxy::CreateDBusPropertiesProxyForTesting();
  EXPECT_CALL(control_, CreateDBusPropertiesProxy(kPath, kService))
      .WillOnce(Return(ByMove(std::move(dbus_properties_proxy))));

  modem_->CreateDevice(properties);
  Cellular* device = modem_->device_for_testing();
  ASSERT_TRUE(device);
  CellularCapability* capability = device->capability_for_testing();
  ASSERT_TRUE(capability);
  EXPECT_TRUE(capability->IsRegistered());
}

}  // namespace shill
