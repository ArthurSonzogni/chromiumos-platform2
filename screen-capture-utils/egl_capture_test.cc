// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <vector>

#include "screen-capture-utils/egl_capture.h"

namespace screenshot {

TEST(EglCaptureTest, Rotate) {
  // The buffer is a screen of width = 4, height = 3:
  //
  // 0 1 2 3
  // 4 5 6 7
  // 8 9 a b
  std::vector<uint32_t> buffer = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb,
  };

  // After rotation by 90 degree clockwise, it should be a screen of
  // width = 3 and height = 4:
  //
  // 8 4 0
  // 9 5 1
  // a 6 2
  // b 7 3
  const std::vector<uint32_t> kExpected = {
      8, 4, 0, 9, 5, 1, 0xa, 6, 2, 0xb, 7, 3,
  };

  DisplayBuffer::Result result{
      .width = 4, .height = 3, .stride = 16, .buffer = buffer.data()};
  std::vector<uint32_t> tmp;
  EglDisplayBuffer::Rotate(result, tmp);
  EXPECT_EQ(kExpected, buffer);
  // Geometoric parameters should be updated accordingly.
  EXPECT_EQ(3, result.width);
  EXPECT_EQ(4, result.height);
  EXPECT_EQ(3 * kBytesPerPixel, result.stride);
}

}  // namespace screenshot
