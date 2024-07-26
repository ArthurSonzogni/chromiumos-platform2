// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_meminfo_reader.h"

#include <gtest/gtest.h>

namespace diagnostics {
namespace {

class FakeMeminfoReaderTest : public ::testing::Test {
 public:
  FakeMeminfoReaderTest(const FakeMeminfoReaderTest&) = delete;
  FakeMeminfoReaderTest& operator=(const FakeMeminfoReaderTest&) = delete;

 protected:
  FakeMeminfoReaderTest() = default;

  FakeMeminfoReader meminfo_reader_;
};

TEST_F(FakeMeminfoReaderTest, NoError) {
  meminfo_reader_.SetError(false);
  EXPECT_TRUE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(FakeMeminfoReaderTest, Error) {
  meminfo_reader_.SetError(true);
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

}  // namespace
}  // namespace diagnostics
