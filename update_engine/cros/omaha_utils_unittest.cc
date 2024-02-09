// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/omaha_utils.h"

#include <gtest/gtest.h>
#include <vector>

namespace chromeos_update_engine {

class OmahaUtilsTest : public ::testing::Test {};

TEST(OmahaUtilsTest, EolDateTest) {
  // Supported values are converted back and forth properly.
  const std::vector<EolDate> tests = {kEolDateInvalid, -1, 0, 1};
  for (EolDate eol_date : tests) {
    EXPECT_EQ(eol_date, StringToEolDate(EolDateToString(eol_date)))
        << "The StringToEolDate() was " << EolDateToString(eol_date);
  }

  // Invalid values are assumed as "supported".
  EXPECT_EQ(kEolDateInvalid, StringToEolDate(""));
  EXPECT_EQ(kEolDateInvalid, StringToEolDate("hello, world!"));
}

}  // namespace chromeos_update_engine
