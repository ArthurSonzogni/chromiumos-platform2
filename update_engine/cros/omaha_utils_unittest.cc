// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <gtest/gtest.h>

#include "update_engine/cros/omaha_utils.h"

namespace chromeos_update_engine {

class OmahaUtilsTest : public ::testing::Test {};

TEST(OmahaUtilsTest, DateTest) {
  // Supported values are converted back and forth properly.
  const std::vector<DateType> tests = {kInvalidDate, -1, 0, 1};
  for (const auto& eol_date : tests) {
    EXPECT_EQ(eol_date, StringToDate(DateToString(eol_date)))
        << "The StringToDate() was " << DateToString(eol_date);
  }

  // Invalid values are converted to `kInvalidDate`.
  EXPECT_EQ(kInvalidDate, StringToDate(""));
  EXPECT_EQ(kInvalidDate, StringToDate("hello, world!"));
}

}  // namespace chromeos_update_engine
