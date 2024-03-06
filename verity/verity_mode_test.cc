// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Tests for verity::FileHasher

#include <gtest/gtest.h>

#include "verity/verity_mode.h"

namespace verity {

TEST(VerityModeTest, ToVerityMode) {
  EXPECT_EQ(VERITY_CREATE, ToVerityMode(kVerityModeCreate));
  EXPECT_EQ(VERITY_VERIFY, ToVerityMode(kVerityModeVerify));
  EXPECT_EQ(VERITY_NONE, ToVerityMode("kVerityModeCreate"));
  EXPECT_EQ(VERITY_NONE, ToVerityMode("gibberish"));
  EXPECT_EQ(VERITY_NONE, ToVerityMode(""));
}

}  // namespace verity
