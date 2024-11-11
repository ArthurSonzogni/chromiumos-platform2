// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fourcc.h"

#include "gtest/gtest.h"

namespace ec {

TEST(FourCC, Valid) {
  ASSERT_EQ(FourCCToString(0x20435046), "FPC ");
  ASSERT_EQ(FourCCToString(0x4e414c45), "ELAN");
  // 8-bit greyscale pixel format as defined by V4L2 headers
  ASSERT_EQ(FourCCToString(0x59455247), "GREY");
  ASSERT_EQ(FourCCToString(0x20202020), "    ");
}

TEST(FourCC, Invalid) {
  ASSERT_EQ(FourCCToString(0x4F3), "0x4F3");
  ASSERT_EQ(FourCCToString(0xFF), "0xFF");
  ASSERT_EQ(FourCCToString(0xFF000000), "0xFF000000");
  ASSERT_EQ(FourCCToString('\t'), "0x9");
  ASSERT_EQ(FourCCToString(0xFFFFFFFF), "0xFFFFFFFF");
}

}  // namespace ec
