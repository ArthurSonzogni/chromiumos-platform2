// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <sane/sane.h>

namespace sane_backend_tests {
// Declared by GoogleTest main wrapper.
extern const std::string* scanner_under_test;
}  // namespace sane_backend_tests

class OptionDescriptorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(
        sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle_),
        SANE_STATUS_GOOD)
        << "Failed to open scanner";
  }

  void TearDown() override { sane_close(handle_); }

  SANE_Handle handle_;
};

TEST_F(OptionDescriptorTest, VerifyOption0) {
  SANE_Int option0_value;
  ASSERT_EQ(sane_control_option(handle_, 0, SANE_ACTION_GET_VALUE,
                                &option0_value, NULL),
            SANE_STATUS_GOOD)
      << "Failed to retrieve option 0";

  EXPECT_GT(option0_value, 0);
}
