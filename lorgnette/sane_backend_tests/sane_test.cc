// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cstdlib>
#include <string>

#include <base/strings/string_util.h>
#include <gtest/gtest.h>
#include <sane/sane.h>
#include <sane/saneopts.h>

// SANETest - Try to only use the SANE API directly ///////////////////////////

namespace sane_backend_tests {
// Declared by GoogleTest main wrapper.
extern const std::string* scanner_under_test;
}  // namespace sane_backend_tests

class SANETest : public testing::Test {
  void SetUp() override {
    SANE_Int _ignored;
    // This is safe because duplicate sane_init calls one after another are
    // safe.
    ASSERT_EQ(sane_init(&_ignored, nullptr), SANE_STATUS_GOOD);
  }

  void TearDown() override {
    // This is safe because duplicate sane_exit calls one after another are
    // safe.
    sane_exit();
  }
};

TEST_F(SANETest, OpenCloseStress) {
  SANE_Handle handle_;

  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(
        sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle_),
        SANE_STATUS_GOOD)
        << "Failed to open scanner on iteration " << i;
    sane_close(handle_);
  }
}
