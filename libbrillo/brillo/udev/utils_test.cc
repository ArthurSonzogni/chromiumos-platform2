// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "brillo/udev/mock_udev_device.h"
#include "brillo/udev/utils.h"

namespace brillo {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;

TEST(UtilsTest, SimpleDeviceRemovableTest) {
  auto non_removable_device = std::make_unique<NiceMock<MockUdevDevice>>();
  EXPECT_CALL(*non_removable_device, GetSysAttributeValue(_))
      .WillOnce(DoAll(Return("0")));

  // Non removable device returns false since it also has no removable
  // parents.
  EXPECT_FALSE(IsRemovable(*non_removable_device));
  auto removable_device = std::make_unique<StrictMock<MockUdevDevice>>();
  EXPECT_CALL(*removable_device, GetSysAttributeValue(StrEq("removable")))
      .WillOnce(Return("1"));
  // Removable device returns success without needing to check parents.
  EXPECT_TRUE(IsRemovable(*removable_device));
}

TEST(UtilsTest, ParentRemovableTest) {
  auto non_removable_device = std::make_unique<StrictMock<MockUdevDevice>>();
  auto removable_device = std::make_unique<StrictMock<MockUdevDevice>>();
  EXPECT_CALL(*non_removable_device, GetSysAttributeValue(_))
      .WillOnce(Return("0"));
  EXPECT_CALL(*removable_device, GetSysAttributeValue(_)).WillOnce(Return("1"));
  EXPECT_CALL(*non_removable_device, GetParent())
      .WillOnce(Return(std::move(removable_device)));

  // Expect removable since the parent device is removable.
  EXPECT_TRUE(IsRemovable(*non_removable_device));
}

}  // namespace brillo
