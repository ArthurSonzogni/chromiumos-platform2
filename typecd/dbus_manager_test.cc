// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/dbus_manager.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "typecd/mock_port.h"
#include "typecd/port_manager.h"
#include "typecd/test_constants.h"
#include "typecd/test_utils.h"

namespace typecd {

class DBusManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
        nullptr, nullptr, dbus::ObjectPath(kTypecdServicePath));
    dbus_manager_ = std::make_unique<DBusManager>(dbus_object_.get());
    port_manager_ = std::make_unique<PortManager>();
    dbus_manager_->SetPortManager(port_manager_.get());

    port_ = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  }

 public:
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  std::unique_ptr<DBusManager> dbus_manager_;
  std::unique_ptr<PortManager> port_manager_;
  std::unique_ptr<MockPort> port_;
};

// Check that |dbus_manager_| can get the board's port count.
TEST_F(DBusManagerTest, DBusGetPortCount) {
  uint32_t port_count;

  // Add |port_| to |port_manager|.
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| get the correct port count.
  EXPECT_TRUE(dbus_manager_->GetPortCount(nullptr, &port_count));
  EXPECT_EQ(1, port_count);
}

// Check that |dbus_manager_| does not return any SOP' alternate modes for a
// non-emarked cable.
TEST_F(DBusManagerTest, DBusGetAltModesUnbrandedUSB2Cable) {
  std::vector<std::tuple<uint16_t, uint32_t>> alt_modes;

  // Setup |port_| and add it to |port_manager|.
  AddUnbrandedUSB2Cable(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns no alternate modes.
  EXPECT_TRUE(dbus_manager_->GetAltModes(
      nullptr, 0, (uint32_t)Recipient::kCable, &alt_modes));
  EXPECT_EQ(0, alt_modes.size());
}

// Check that |dbus_manager_| returns expected SOP' alternate modes for the
// Apple TBT3 Pro cable.
TEST_F(DBusManagerTest, DBusGetAltModesAppleTBT3ProCable) {
  std::vector<std::tuple<uint16_t, uint32_t>> alt_modes;

  // Setup |port_| and add it to |port_manager|.
  AddAppleTBT3ProCable(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns that cable's 5 alternate modes.
  EXPECT_TRUE(dbus_manager_->GetAltModes(
      nullptr, 0, (uint32_t)Recipient::kCable, &alt_modes));
  EXPECT_EQ(5, alt_modes.size());
  EXPECT_EQ(0x8087, get<0>(alt_modes[0]));
  EXPECT_EQ(0x00cb0001, get<1>(alt_modes[0]));
  EXPECT_EQ(0xff01, get<0>(alt_modes[1]));
  EXPECT_EQ(0x000c0c0c, get<1>(alt_modes[1]));
  EXPECT_EQ(0x05ac, get<0>(alt_modes[2]));
  EXPECT_EQ(0x00000005, get<1>(alt_modes[2]));
  EXPECT_EQ(0x05ac, get<0>(alt_modes[3]));
  EXPECT_EQ(0x00000007, get<1>(alt_modes[3]));
  EXPECT_EQ(0x05ac, get<0>(alt_modes[4]));
  EXPECT_EQ(0x00000002, get<1>(alt_modes[4]));
}

// Check that |dbus_manager_| can return expected SOP alternate modes for the
// OWC TBT4 dock.
TEST_F(DBusManagerTest, DBusGetAltModesOWCTBT4Dock) {
  std::vector<std::tuple<uint16_t, uint32_t>> alt_modes;

  // Setup |port_| and add it to |port_manager|.
  AddOWCTBT4Dock(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns that partner's 2 alternate modes.
  EXPECT_TRUE(dbus_manager_->GetAltModes(
      nullptr, 0, (uint32_t)Recipient::kPartner, &alt_modes));
  EXPECT_EQ(2, alt_modes.size());
  EXPECT_EQ(0xff01, get<0>(alt_modes[0]));
  EXPECT_EQ(0x001c0045, get<1>(alt_modes[0]));
  EXPECT_EQ(0x8087, get<0>(alt_modes[1]));
  EXPECT_EQ(0x00000001, get<1>(alt_modes[1]));
}

// Check that |dbus_manager_| can return the current port mode for a port using
// DisplayPort alternate mode.
TEST_F(DBusManagerTest, DBusGetCurrentModeDpAltMode) {
  uint32_t mode;

  // Setup |port_| and add it to |port_manager|.
  AddWimaxitDisplay(*port_);
  port_->SetCurrentMode(TypeCMode::kDP);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns DPAM.
  EXPECT_TRUE(dbus_manager_->GetCurrentMode(nullptr, 0, &mode));
  EXPECT_EQ((uint32_t)USBCMode::kDP, mode);
}

// Check that |dbus_manager_| can return the current port mode for a port using
// USB4.
TEST_F(DBusManagerTest, DBusGetCurrentModeUSB4Mode) {
  uint32_t mode;

  // Setup |port_| and add it to |port_manager|.
  AddOWCTBT4Dock(*port_);
  port_->SetCurrentMode(TypeCMode::kUSB4);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns USB4.
  EXPECT_TRUE(dbus_manager_->GetCurrentMode(nullptr, 0, &mode));
  EXPECT_EQ((uint32_t)USBCMode::kUSB4, mode);
}

// Check that |dbus_manager_| can return the identity of a non-emarked USB 2.0
// cable.
TEST_F(DBusManagerTest, DBusGetIdentityUnbrandedUSB2Cable) {
  std::vector<uint32_t> identity;

  // Setup |port_| and add it to |port_manager|.
  AddUnbrandedUSB2Cable(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns zero for all identity VDOs.
  EXPECT_TRUE(dbus_manager_->GetIdentity(
      nullptr, 0, (uint32_t)Recipient::kCable, &identity));
  EXPECT_EQ(0x0, identity[0]);
  EXPECT_EQ(0x0, identity[1]);
  EXPECT_EQ(0x0, identity[2]);
  EXPECT_EQ(0x0, identity[3]);
  EXPECT_EQ(0x0, identity[4]);
  EXPECT_EQ(0x0, identity[5]);
}

// Check that |dbus_manager_| can return the identity of an emarked USB 3.2
// cable.
TEST_F(DBusManagerTest, DBusGetIdentityAnkerUSB3p2Gen2Cable) {
  std::vector<uint32_t> identity;

  // Setup |port_| and add it to |port_manager|.
  AddAnkerUSB3p2Gen2Cable(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns the cable's identity.
  EXPECT_TRUE(dbus_manager_->GetIdentity(
      nullptr, 0, (uint32_t)Recipient::kCable, &identity));
  EXPECT_EQ(0x1c00291a, identity[0]);
  EXPECT_EQ(0xd0b, identity[1]);
  EXPECT_EQ(0x1ff90000, identity[2]);
  EXPECT_EQ(0x11082032, identity[3]);
  EXPECT_EQ(0x0, identity[4]);
  EXPECT_EQ(0x0, identity[5]);
}

// Check that |dbus_manager_| can return the identity of a Cable Matters DPAM
// dock.
TEST_F(DBusManagerTest, DBusGetIdentityCableMatterDock) {
  std::vector<uint32_t> identity;

  // Setup |port_| and add it to |port_manager|.
  AddCableMattersDock(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns the dock's identity.
  EXPECT_TRUE(dbus_manager_->GetIdentity(
      nullptr, 0, (uint32_t)Recipient::kPartner, &identity));
  EXPECT_EQ(0x6c0004b4, identity[0]);
  EXPECT_EQ(0x0, identity[1]);
  EXPECT_EQ(0xf6490222, identity[2]);
  EXPECT_EQ(0x8, identity[3]);
  EXPECT_EQ(0x0, identity[4]);
  EXPECT_EQ(0x0, identity[5]);
}

// Check that |dbus_manager_| can return the physical location of a port.
TEST_F(DBusManagerTest, DBusGetPLD) {
  std::tuple<uint8_t, uint8_t, uint8_t> pld;

  // Setup |port_| and add it to |port_manager|. The mock port overrides getter
  // PLD functions, so use EXPECT_CALL to return a valid PLD.
  EXPECT_CALL(*port_, GetPanel()).WillRepeatedly(testing::Return(Panel::kLeft));
  EXPECT_CALL(*port_, GetHorizontalPosition())
      .WillRepeatedly(testing::Return(HorizontalPosition::kLeft));
  EXPECT_CALL(*port_, GetVerticalPosition())
      .WillRepeatedly(testing::Return(VerticalPosition::kUpper));
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns the port's physical location.
  EXPECT_TRUE(dbus_manager_->GetPLD(nullptr, 0, &pld));
  EXPECT_EQ((uint8_t)Panel::kLeft, get<0>(pld));
  EXPECT_EQ((uint8_t)HorizontalPosition::kLeft, get<1>(pld));
  EXPECT_EQ((uint8_t)VerticalPosition::kUpper, get<2>(pld));
}

// Check that |dbus_manager_| can get the PD revision of a USB PD 2.0 cable.
TEST_F(DBusManagerTest, DBusGetRevisionPD2p0) {
  uint16_t revision;

  // Setup |port_| and add it to |port_manager|.
  AddStartechTB3DK2DPWDock(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns USB PD 2.0 for the dock.
  EXPECT_TRUE(dbus_manager_->GetRevision(
      nullptr, 0, (uint32_t)Recipient::kPartner, &revision));
  EXPECT_EQ(kPDRevision20, revision);
}

// Check that |dbus_manager_| can get the PD revision of a USB PD 3.1 partner.
TEST_F(DBusManagerTest, DBusGetRevisionPD3p1) {
  uint16_t revision;

  // Setup |port_| and add it to |port_manager|.
  AddHPG4Dock(*port_);
  port_manager_->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port_)));

  // Confirm |dbus_manager_| returns USB PD 3.1 for the dock.
  EXPECT_TRUE(dbus_manager_->GetRevision(
      nullptr, 0, (uint32_t)Recipient::kPartner, &revision));
  EXPECT_EQ(kPDRevision31, revision);
}

}  // namespace typecd
