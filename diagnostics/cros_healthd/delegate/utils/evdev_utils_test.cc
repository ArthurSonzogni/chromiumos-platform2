// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"

#include <linux/input-event-codes.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/delegate/utils/test/mock_libevdev_wrapper.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SizeIs;
using ::testing::StrictMock;

void SetMockSlotValue(test::MockLibevdevWrapper* dev,
                      int slot,
                      int code,
                      int value) {
  EXPECT_CALL(*dev, FetchSlotValue(slot, code, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(value), Return(1)));
}

void SetMockSlotValue(test::MockLibevdevWrapper* dev,
                      int slot,
                      int code,
                      const mojom::NullableUint32Ptr& value) {
  if (value.is_null()) {
    EXPECT_CALL(*dev, FetchSlotValue(slot, code, _)).WillRepeatedly(Return(0));
  } else {
    SetMockSlotValue(dev, slot, code, value->value);
  }
}

void SetMockTouchPointInfo(test::MockLibevdevWrapper* dev,
                           int slot,
                           const mojom::TouchPointInfoPtr& info) {
  SetMockSlotValue(dev, slot, ABS_MT_TRACKING_ID, info->tracking_id);
  SetMockSlotValue(dev, slot, ABS_MT_POSITION_X, info->x);
  SetMockSlotValue(dev, slot, ABS_MT_POSITION_Y, info->y);
  SetMockSlotValue(dev, slot, ABS_MT_PRESSURE, info->pressure);
  SetMockSlotValue(dev, slot, ABS_MT_TOUCH_MAJOR, info->touch_major);
  SetMockSlotValue(dev, slot, ABS_MT_TOUCH_MINOR, info->touch_minor);
}

TEST(EvdevUtilsTouchPointTest,
     FetchTouchPointsReturnsEmptyListIfNumberOfSlotsIsInvalid) {
  StrictMock<test::MockLibevdevWrapper> libevdev_wrapper;
  EXPECT_CALL(libevdev_wrapper, GetNumSlots).WillRepeatedly(Return(-1));

  auto res = FetchTouchPoints(&libevdev_wrapper);
  EXPECT_THAT(res, IsEmpty());
}

TEST(EvdevUtilsTouchPointTest, FetchTouchPointsReturnsEmptyListIfNoSlots) {
  StrictMock<test::MockLibevdevWrapper> libevdev_wrapper;
  EXPECT_CALL(libevdev_wrapper, GetNumSlots).WillRepeatedly(Return(0));

  auto res = FetchTouchPoints(&libevdev_wrapper);
  EXPECT_THAT(res, IsEmpty());
}

TEST(EvdevUtilsTouchPointTest, FetchSingleTouchPointsSuccessfully) {
  StrictMock<test::MockLibevdevWrapper> libevdev_wrapper;

  auto expected = mojom::TouchPointInfo::New();
  expected->tracking_id = 1;
  expected->x = 2;
  expected->y = 3;
  expected->pressure = mojom::NullableUint32::New(4);
  expected->touch_major = mojom::NullableUint32::New(5);
  expected->touch_minor = mojom::NullableUint32::New(6);

  EXPECT_CALL(libevdev_wrapper, GetNumSlots()).WillRepeatedly(Return(1));
  SetMockTouchPointInfo(&libevdev_wrapper, 0, expected);

  auto res = FetchTouchPoints(&libevdev_wrapper);
  ASSERT_THAT(res, SizeIs(1));

  const auto& got = res[0];
  EXPECT_EQ(got, expected);
}

TEST(EvdevUtilsTouchPointTest, FetchMultipleTouchPointsSuccessfully) {
  constexpr int kNumberOfSlots = 5;
  StrictMock<test::MockLibevdevWrapper> libevdev_wrapper;

  std::vector<mojom::TouchPointInfoPtr> expected_points;
  for (int i = 0; i < kNumberOfSlots; ++i) {
    auto info = mojom::TouchPointInfo::New();
    info->tracking_id = i;
    expected_points.push_back(std::move(info));
  }

  EXPECT_CALL(libevdev_wrapper, GetNumSlots())
      .WillRepeatedly(Return(expected_points.size()));
  for (int i = 0; i < expected_points.size(); ++i) {
    SetMockTouchPointInfo(&libevdev_wrapper, i, expected_points[i]);
  }

  auto res = FetchTouchPoints(&libevdev_wrapper);
  EXPECT_EQ(res, expected_points);
}

// Negative tracking IDs indicate non-contact points.
TEST(EvdevUtilsTouchPointTest, FetchTouchPointsIgnoresNegativeTrackingIds) {
  StrictMock<test::MockLibevdevWrapper> libevdev_wrapper;

  auto non_contact_point = mojom::TouchPointInfo::New();
  non_contact_point->tracking_id = -1;

  EXPECT_CALL(libevdev_wrapper, GetNumSlots()).WillRepeatedly(Return(1));
  SetMockTouchPointInfo(&libevdev_wrapper, 0, non_contact_point);

  auto res = FetchTouchPoints(&libevdev_wrapper);
  EXPECT_THAT(res, IsEmpty());
}

}  // namespace
}  // namespace diagnostics
