// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem.h"

#include <utility>

#include <base/memory/scoped_refptr.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus-proxy-mocks.h>

#include "modemfwd/mock_modem_helper.h"
#include "modemfwd/modem_helper_directory_stub.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;
using ::testing::SetArgPointee;

namespace modemfwd {

class ModemTest : public ::testing::Test {
 public:
  ModemTest()
      : device_(new org::chromium::flimflam::DeviceProxyMock),
        helper_directory_(new ModemHelperDirectoryStub) {
    bus_ = base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options());

    ON_CALL(*device_, GetObjectPath())
        .WillByDefault(ReturnRefOfCopy(dbus::ObjectPath("/fake/device")));
  }

  std::unique_ptr<Modem> GetModem() {
    return CreateModem(bus_.get(), std::move(device_), helper_directory_.get());
  }

 protected:
  scoped_refptr<dbus::MockBus> bus_;
  std::unique_ptr<org::chromium::flimflam::DeviceProxyMock> device_;
  std::unique_ptr<ModemHelperDirectoryStub> helper_directory_;
};

TEST_F(ModemTest, FailToGetProperties) {
  EXPECT_CALL(*device_, GetProperties(_, _, _)).WillOnce(Return(false));
  EXPECT_EQ(GetModem(), nullptr);
}

TEST_F(ModemTest, NoDeviceId) {
  brillo::VariantDictionary props;
  EXPECT_CALL(*device_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_EQ(GetModem(), nullptr);
}

TEST_F(ModemTest, NoEquipmentId) {
  brillo::VariantDictionary props;
  props[shill::kDeviceIdProperty] = "device_id";
  EXPECT_CALL(*device_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_EQ(GetModem(), nullptr);
}

TEST_F(ModemTest, NoModemManagerObject) {
  brillo::VariantDictionary props;
  props[shill::kDeviceIdProperty] = "device_id";
  props[shill::kEquipmentIdProperty] = "equip_id";
  EXPECT_CALL(*device_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_EQ(GetModem(), nullptr);
}

TEST_F(ModemTest, NoHelper) {
  brillo::VariantDictionary props;
  props[shill::kDeviceIdProperty] = "device_id";
  props[shill::kEquipmentIdProperty] = "equip_id";
  props[shill::kDBusObjectProperty] = "/mm/object";
  EXPECT_CALL(*device_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_EQ(GetModem(), nullptr);
}

TEST_F(ModemTest, NoFirmwareInfo) {
  constexpr char kDeviceId[] = "device_id";
  MockModemHelper modem_helper_;
  helper_directory_->AddHelper(kDeviceId, &modem_helper_);

  brillo::VariantDictionary props;
  props[shill::kDeviceIdProperty] = kDeviceId;
  props[shill::kEquipmentIdProperty] = "equip_id";
  props[shill::kDBusObjectProperty] = "/mm/object";
  EXPECT_CALL(*device_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));

  EXPECT_EQ(GetModem(), nullptr);
}

}  // namespace modemfwd
