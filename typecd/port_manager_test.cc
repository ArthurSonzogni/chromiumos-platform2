// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port_manager.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "typecd/mock_ec_util.h"
#include "typecd/mock_port.h"

using ::testing::_;
using ::testing::Return;
using ::testing::Sequence;

namespace typecd {

class PortManagerTest : public ::testing::Test {};

// Test the basic case where mode entry is not supported
// by the ECUtil implementation.
TEST_F(PortManagerTest, ModeEntryNotSupported) {
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(_, _)).Times(0);
  EXPECT_CALL(*ec_util, ExitMode(_)).Times(0);

  auto port_manager = std::make_unique<PortManager>();
  port_manager->SetECUtil(ec_util.get());

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(false);

  // It doesn't matter that we haven't registered any ports, since the code
  // should return before this is checked.
  port_manager->RunModeEntry(0);

  // There is no explicit test here, just that the Mock expectations should be
  // met.
}

// Test the basic case of "active" user hotplug mode entry for the following
// scenarios:
// - Only DP supported.
// - Only TBT supported.
// - Both DP & TBT supported.
TEST_F(PortManagerTest, SimpleModeEntry) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Create the MockECUtil and set the expectations (enter DP called once).
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kDP))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(_)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports only DP.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterDPAltMode(_))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // Assume that the user is active.
  port_manager->SetUserActive(true);

  // Simulate a hotplug.
  port_manager->RunModeEntry(0);

  // Update the MockECUtil to check for TBT entry.
  ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kTBT))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(_)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Replace with a fake port that supports only TBT.
  port_manager->ports_.erase(0);
  port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(false));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // Simulate a hotplug.
  port_manager->RunModeEntry(0);

  // Update the MockECUtil to check for TBT entry again.
  // NOTE: If both DP & TBT are supported, and this is unlocked hotplug, then
  // TBT should be picked.
  ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kTBT))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(_)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Replace with a fake port that supports both DP & TBT.
  port_manager->ports_.erase(0);
  port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // Simulate a hotplug.
  port_manager->RunModeEntry(0);

  // There is no explicit test here, just that the mock expectations should be
  // met.
}

// Check mode switch on unlock for a device which was:
// - plugged in while locked.
// - supports both TBT and DP.
TEST_F(PortManagerTest, ModeSwitchUnlockDPandTBT) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Create the MockECUtil and set the expectations:
  // first enter DP, then exit (on unlock), and then enter TBT.
  Sequence s1;
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kDP))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kTBT))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports both TBT & DP.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are on a lock screen, so set |user_active_| accordingly.
  port_manager->SetUserActive(false);
  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Simulate unlock (just call the unlock callback since we don't have a
  // SessionManager callback).
  port_manager->HandleUnlock();
}

// Check mode switch on unlock for a device which was:
// - plugged in while locked.
// - supports USB4.
TEST_F(PortManagerTest, ModeSwitchUnlockUSB4) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Create the MockECUtil and set the expectations:
  // Since this is USB4, we expect only 1 EnterMode call and no ExitMode calls.
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kUSB4))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports only USB4.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(false));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are on a lock screen, so set |user_active_| accordingly.
  port_manager->SetUserActive(false);
  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Simulate unlock (just call the unlock callback since we don't have a
  // SessionManager callback).
  port_manager->HandleUnlock();
}

// Check mode switch on "session stopped" for a device which was:
// - plugged in while the user session was ongoing (screen was unlocked).
// - supports both TBT and DP.
TEST_F(PortManagerTest, ModeSwitchSessionStoppedDPandTBT) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Create the MockECUtil and set the expectations:
  // first enter TBT, then exit (on session stopped), and then enter DP.
  Sequence s1;
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kTBT))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kDP))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports both TBT & DP.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are on a unlocked screen, so set |user_active_| accordingly.
  port_manager->SetUserActive(true);
  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Simulate session stopped (just call the session stopped callback since we
  // don't have a SessionManager callback).
  port_manager->HandleSessionStopped();
}

// Check mode switch on "session stopped" for a device which was:
// - plugged in while the user session was ongoing (screen was unlocked).
// - supports TBT only.
TEST_F(PortManagerTest, ModeSwitchSessionStoppedTBT) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Create the MockECUtil and set the expectations:
  // Since this is , we expect only 1 EnterMode call and no ExitMode calls.
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kTBT))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports only TBT.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(false));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are on a unlocked screen, so set |user_active_| accordingly.
  port_manager->SetUserActive(true);
  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Simulate session stopped (just call the session stopped callback since we
  // don't have a SessionManager callback).
  port_manager->HandleSessionStopped();
}

// Check mode switch on unlock for a device which was:
// - plugged in while locked.
// - supports both TBT & DP.
// - peripheral data access is set to "false".
//
// In this case, no mode switches should occur.
TEST_F(PortManagerTest, ModeSwitchUnlockDPAndTBTNoPeripheralAccess) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Manually set the |peripheral_data_access_| field.
  port_manager->SetPeripheralDataAccess(false);

  // Create the MockECUtil and set the expectations:
  // Since this is TBT+DP, with peripheral data access set to "false", we expect
  // only 1 EnterMode call and no ExitMode calls.
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kDP))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports TBT & DP.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are on a lock screen, so set |user_active_| accordingly.
  port_manager->SetUserActive(false);

  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Simulate unlock (just call the unlock callback since we don't have a
  // SessionManager callback).
  port_manager->HandleUnlock();
}

// Check mode switch for a device which was:
// - plugged in while unlocked.
// - supports both TBT and DP.
// - a subsequent logout and then log in occurs.
//
// Additionally, we add the following test conditions:
// - Before the device was plugged in, peripheral data access was disabled.
// - After the device was plugged in, but before logout, peripheral data access
//   was enabled.
TEST_F(PortManagerTest, ModeSwitchDPandTBTPeripheralDataAccessChanging) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Manually set the |peripheral_data_access_| field, initially to false.
  port_manager->SetPeripheralDataAccess(false);

  // Create the MockECUtil and set the expectations:
  // first enter DP, then exit (on logout), and then enter TBT on subsequent
  // login.
  Sequence s1;
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kDP))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kTBT))
      .InSequence(s1)
      .WillOnce(testing::Return(true));
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports both TBT & DP.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are unlocked, so set |user_active_| accordingly.
  port_manager->SetUserActive(true);

  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Flip the |peripheral_data_access_| field to true.
  port_manager->SetPeripheralDataAccess(true);

  // Simulate logout (just call the session stopped callback since we don't have
  // a SessionManager).
  port_manager->HandleSessionStopped();

  // Simulate login (just call the session started callback since we don't have
  // a SessionManager)
  port_manager->HandleUnlock();
}

// Check mode switch for a device which was:
// - plugged in while unlocked.
// - supports both TBT and DP.
// - a subsequent lock and then unlock occurs.
//
// Additionally, we add the following test conditions:
// - Before the device was plugged in, peripheral data access was disabled.
// - After the device was plugged in, but before lock, peripheral data access
//   was enabled.
TEST_F(PortManagerTest,
       ModeSwitchDPandTBTPeripheralDataAccessChangingLockUnlock) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Manually set the |peripheral_data_access_| field, initially to false.
  port_manager->SetPeripheralDataAccess(false);

  // Create the MockECUtil and set the expectations:
  // first enter DP, then exit (on logout), and then enter TBT on subsequent
  // login.
  Sequence s1;
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kDP))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports both TBT & DP.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are unlocked, so set |user_active_| accordingly.
  port_manager->SetUserActive(true);

  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Flip the |peripheral_data_access_| field to true.
  port_manager->SetPeripheralDataAccess(true);

  // Simulate lock (just call the OnScreenLocked callback since we don't have
  // a SessionManager).
  port_manager->OnScreenIsLocked();

  // Simulate unlock.
  port_manager->HandleUnlock();
}

// Check mode switch for a device which was:
// - plugged in while unlocked.
// - supports only TBT.
// - a subsequent logout and then log in occurs.
//
// Additionally, we add the following test conditions:
// - Before the device was plugged in, peripheral data access was disabled.
// - After the device was plugged in, but before logout, peripheral data access
//   was enabled.
TEST_F(PortManagerTest, ModeSwitchTBTPeripheralDataAccessChanging) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Manually set the |peripheral_data_access_| field, initially to false.
  port_manager->SetPeripheralDataAccess(false);

  // Create the MockECUtil and set the expectations:
  // Since the device only supports TBT, there should be just one call to
  // EnterMode for TBT and no calls to ExitMode.
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kTBT))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(0)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that supports only TBT.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPartnerError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kSuccess));
  EXPECT_CALL(*port, CanEnterDPAltMode(nullptr))
      .WillRepeatedly(testing::Return(false));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // We are unlocked, so set |user_active_| accordingly.
  port_manager->SetUserActive(true);

  // Simulate hotplug.
  port_manager->RunModeEntry(0);

  // Flip the |peripheral_data_access_| field to true.
  port_manager->SetPeripheralDataAccess(true);

  // Simulate logout (just call the session stopped callback since we don't have
  // a SessionManager).
  port_manager->HandleSessionStopped();

  // Simulate login (just call the session started callback since we don't have
  // a SessionManager).
  port_manager->HandleUnlock();
}

// Test the case of "active" user hotplug mode entry for the following
// scenario:
// - USB4 & TBT is supported, but the system only supports DP.
TEST_F(PortManagerTest, ModeEntryDPOnlySystem) {
  auto port_manager = std::make_unique<PortManager>();

  // Since we only have a MockECUtil, just force the |mode_entry_supported_|
  // flag.
  port_manager->SetModeEntrySupported(true);

  // Create the MockECUtil and set the expectations (enter DP called once).
  auto ec_util = std::make_unique<MockECUtil>();
  EXPECT_CALL(*ec_util, ModeEntrySupported()).Times(0);
  EXPECT_CALL(*ec_util, EnterMode(0, TypeCMode::kDP))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*ec_util, ExitMode(_)).Times(0);
  port_manager->SetECUtil(ec_util.get());

  // Add a fake port that only supports DP mode entry because of system
  // limitations.
  auto port = std::make_unique<MockPort>(base::FilePath("fakepath"), 0);
  EXPECT_CALL(*port, GetDataRole())
      .WillRepeatedly(testing::Return(DataRole::kHost));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPortError));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(ModeEntryResult::kPortError));
  EXPECT_CALL(*port, CanEnterDPAltMode(_))
      .WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // Assume that the user is active.
  port_manager->SetUserActive(true);

  // Simulate a hotplug.
  port_manager->RunModeEntry(0);

  // There is no explicit test here, just that the mock expectations should be
  // met.
}

}  // namespace typecd
