// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/path_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/service_config.h"
#include "diagnostics/cros_healthd/system/cros_config.h"

namespace diagnostics {
namespace {

constexpr auto kTestPath = MakePathLiteral("a", "b", "c");

namespace paths = paths::cros_config;

using CrosConfigTest = BaseFileTest;

TEST_F(CrosConfigTest, NotFound) {
  CrosConfig cros_config{ServiceConfig{}};
  EXPECT_FALSE(cros_config.Get(kTestPath));
}

TEST_F(CrosConfigTest, Found) {
  SetFile(MakePathLiteral(paths::kRoot, kTestPath), "FakeData");
  CrosConfig cros_config{ServiceConfig{}};
  EXPECT_EQ(cros_config.Get(kTestPath), "FakeData");
}

TEST_F(CrosConfigTest, TestNotFound) {
  CrosConfig cros_config{ServiceConfig{.test_cros_config = true}};
  EXPECT_FALSE(cros_config.Get(kTestPath));
}

TEST_F(CrosConfigTest, TestFound) {
  SetFile(MakePathLiteral(paths::kTestRoot, kTestPath), "FakeData");
  CrosConfig cros_config{ServiceConfig{.test_cros_config = true}};
  EXPECT_EQ(cros_config.Get(kTestPath), "FakeData");
}

}  // namespace
}  // namespace diagnostics
