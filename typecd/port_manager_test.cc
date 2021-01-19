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
      .WillRepeatedly(testing::Return(std::string("host")));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4()).WillRepeatedly(testing::Return(false));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(*port, CanEnterDPAltMode()).WillRepeatedly(testing::Return(true));
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
      .WillRepeatedly(testing::Return(std::string("host")));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4()).WillRepeatedly(testing::Return(false));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterDPAltMode())
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
      .WillRepeatedly(testing::Return(std::string("host")));
  EXPECT_CALL(*port, IsPartnerDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, IsCableDiscoveryComplete())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterUSB4()).WillRepeatedly(testing::Return(false));
  EXPECT_CALL(*port, CanEnterTBTCompatibilityMode())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*port, CanEnterDPAltMode()).WillRepeatedly(testing::Return(true));
  port_manager->ports_.insert(
      std::pair<int, std::unique_ptr<Port>>(0, std::move(port)));

  // Simulate a hotplug.
  port_manager->RunModeEntry(0);

  // There is no explicit test here, just that the mock expectations should be
  // met.
}

}  // namespace typecd
