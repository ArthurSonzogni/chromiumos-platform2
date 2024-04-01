/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/common_types.h"

#include <gtest/gtest.h>

namespace cros {
namespace {

TEST(RelativeFovTest, ActiveArray4x3) {
  const Size active_array_size(2592, 1944);

  EXPECT_EQ(RelativeFov(active_array_size, active_array_size),
            RelativeFov(1.0f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(1600, 1200), active_array_size),
            RelativeFov(1.0f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(640, 480), active_array_size),
            RelativeFov(1.0f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(4, 3), active_array_size),
            RelativeFov(1.0f, 1.0f));

  EXPECT_EQ(RelativeFov(Size(1920, 1080), active_array_size),
            RelativeFov(1.0f, 0.75f));
  EXPECT_EQ(RelativeFov(Size(1280, 720), active_array_size),
            RelativeFov(1.0f, 0.75f));
  EXPECT_EQ(RelativeFov(Size(16, 9), active_array_size),
            RelativeFov(1.0f, 0.75f));

  EXPECT_EQ(RelativeFov(Size(256, 256), active_array_size),
            RelativeFov(0.75f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(1, 1), active_array_size),
            RelativeFov(0.75f, 1.0f));
}

TEST(RelativeFovTest, ActiveArray16x9) {
  const Size active_array_size(1920, 1080);

  EXPECT_EQ(RelativeFov(active_array_size, active_array_size),
            RelativeFov(1.0f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(1920, 1080), active_array_size),
            RelativeFov(1.0f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(1280, 720), active_array_size),
            RelativeFov(1.0f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(16, 9), active_array_size),
            RelativeFov(1.0f, 1.0f));

  EXPECT_EQ(RelativeFov(Size(1600, 1200), active_array_size),
            RelativeFov(0.75f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(640, 480), active_array_size),
            RelativeFov(0.75f, 1.0f));
  EXPECT_EQ(RelativeFov(Size(4, 3), active_array_size),
            RelativeFov(0.75f, 1.0f));

  EXPECT_EQ(RelativeFov(Size(256, 128), active_array_size),
            RelativeFov(1.0f, 0.8889f));
  EXPECT_EQ(RelativeFov(Size(2, 1), active_array_size),
            RelativeFov(1.0f, 0.8889f));
}

TEST(RelativeFovTest, Covering) {
  EXPECT_TRUE(RelativeFov(1.0f, 1.0f).Covers(RelativeFov(1.0f, 1.0f)));
  EXPECT_TRUE(RelativeFov(1.0f, 1.0f).Covers(RelativeFov(0.9f, 1.0f)));
  EXPECT_TRUE(RelativeFov(1.0f, 1.0f).Covers(RelativeFov(1.0f, 0.9f)));

  EXPECT_TRUE(RelativeFov(0.9f, 1.0f).Covers(RelativeFov(0.9f, 1.0f)));
  EXPECT_TRUE(RelativeFov(0.9f, 1.0f).Covers(RelativeFov(0.8f, 1.0f)));
  EXPECT_FALSE(RelativeFov(0.9f, 1.0f).Covers(RelativeFov(1.0f, 1.0f)));
  EXPECT_FALSE(RelativeFov(0.9f, 1.0f).Covers(RelativeFov(1.0f, 0.9f)));

  EXPECT_TRUE(RelativeFov(1.0f, 0.9f).Covers(RelativeFov(1.0f, 0.9f)));
  EXPECT_TRUE(RelativeFov(1.0f, 0.9f).Covers(RelativeFov(1.0f, 0.8f)));
  EXPECT_FALSE(RelativeFov(1.0f, 0.9f).Covers(RelativeFov(1.0f, 1.0f)));
  EXPECT_FALSE(RelativeFov(1.0f, 0.9f).Covers(RelativeFov(0.9f, 1.0f)));
}

TEST(RelativeFov, CropWindow) {
  EXPECT_EQ(RelativeFov(1.0f, 1.0f).GetCropWindowInto(RelativeFov(1.0f, 1.0f)),
            Rect<float>(0.0f, 0.0f, 1.0f, 1.0f));
  EXPECT_EQ(RelativeFov(1.0f, 1.0f).GetCropWindowInto(RelativeFov(0.75f, 1.0f)),
            Rect<float>(0.125, 0.0f, 0.75f, 1.0f));
  EXPECT_EQ(RelativeFov(1.0f, 1.0f).GetCropWindowInto(RelativeFov(1.0f, 0.75f)),
            Rect<float>(0.0f, 0.125, 1.0f, 0.75f));

  EXPECT_EQ(
      RelativeFov(0.75f, 1.0f).GetCropWindowInto(RelativeFov(0.75f, 1.0f)),
      Rect<float>(0.0f, 0.0f, 1.0f, 1.0f));
  EXPECT_EQ(
      RelativeFov(0.75f, 1.0f).GetCropWindowInto(RelativeFov(0.5625f, 1.0f)),
      Rect<float>(0.125f, 0.0f, 0.75f, 1.0f));

  EXPECT_EQ(
      RelativeFov(1.0f, 0.75f).GetCropWindowInto(RelativeFov(1.0f, 0.75f)),
      Rect<float>(0.0f, 0.0f, 1.0f, 1.0f));
  EXPECT_EQ(
      RelativeFov(1.0f, 0.75f).GetCropWindowInto(RelativeFov(1.0f, 0.6667f)),
      Rect<float>(0.0f, 0.0556f, 1.0f, 0.8889));

  EXPECT_DEATH(
      RelativeFov(0.9f, 1.0f).GetCropWindowInto(RelativeFov(1.0f, 0.9f)), "");
}

}  // namespace
}  // namespace cros

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
