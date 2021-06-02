// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <gtest/gtest.h>

#include "sommelier.h"  // NOLINT(build/include_directory)

namespace vm_tools {
namespace sommelier {

class SommelierTest : public ::testing::Test {};

TEST_F(SommelierTest, TestNowt) {
  sl_context ctx;
  sl_context_init_default(&ctx);
  std::cout << "Hi!";
}

}  // namespace sommelier
}  // namespace vm_tools

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::GTEST_FLAG(throw_on_failure) = true;
  // TODO(nverne): set up logging?
  return RUN_ALL_TESTS();
}
