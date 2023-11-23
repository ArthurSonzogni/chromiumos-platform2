// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/types/expected.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/path_literal.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/service_config.h"
#include "diagnostics/cros_healthd/system/cros_config.h"
#include "diagnostics/cros_healthd/system/cros_config_constants.h"

namespace diagnostics {
namespace {

constexpr auto kTestPath = MakePathLiteral("a", "b", "c");

namespace paths = paths::cros_config;

class CrosConfigTest : public BaseFileTest {
 protected:
  CrosConfig cros_config_{ServiceConfig{}};
};

TEST_F(CrosConfigTest, NotFound) {
  EXPECT_FALSE(cros_config_.Get(kTestPath));
}

TEST_F(CrosConfigTest, Found) {
  SetFakeCrosConfig(kTestPath, "FakeData");

  EXPECT_EQ(cros_config_.Get(kTestPath), "FakeData");
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

TEST_F(CrosConfigTest, CheckExpectedCrosConfig) {
  SetFakeCrosConfig(kTestPath, "FakeData");
  EXPECT_TRUE(
      cros_config_.CheckExpectedCrosConfig(kTestPath, "FakeData").has_value());

  SetFakeCrosConfig(kTestPath, "NotMatch");
  EXPECT_EQ(cros_config_.CheckExpectedCrosConfig(kTestPath, "FakeData").error(),
            "Expected cros_config property [a/b/c] to be [FakeData], but got "
            "[NotMatch]");

  SetFakeCrosConfig(kTestPath, std::nullopt);
  EXPECT_EQ(cros_config_.CheckExpectedCrosConfig(kTestPath, "FakeData").error(),
            "Expected cros_config property [a/b/c] to be [FakeData], but got "
            "[]");
}

TEST_F(CrosConfigTest, CheckExpectedsCrosConfig) {
  SetFakeCrosConfig(kTestPath, "A");
  EXPECT_TRUE(
      cros_config_.CheckExpectedsCrosConfig(kTestPath, {"A", "B"}).has_value());

  SetFakeCrosConfig(kTestPath, "NotMatch");
  EXPECT_EQ(
      cros_config_.CheckExpectedsCrosConfig(kTestPath, {"A", "B"}).error(),
      "Expected cros_config property [a/b/c] to be [A] or [B], but got "
      "[NotMatch]");

  SetFakeCrosConfig(kTestPath, std::nullopt);
  EXPECT_EQ(
      cros_config_.CheckExpectedsCrosConfig(kTestPath, {"A", "B"}).error(),
      "Expected cros_config property [a/b/c] to be [A] or [B], but got "
      "[]");
}

TEST_F(CrosConfigTest, CheckTrueCrosConfig) {
  SetFakeCrosConfig(kTestPath, cros_config_value::kTrue);
  EXPECT_TRUE(cros_config_.CheckTrueCrosConfig(kTestPath).has_value());

  SetFakeCrosConfig(kTestPath, "not_true");
  EXPECT_EQ(cros_config_.CheckTrueCrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [true], but got "
            "[not_true]");

  SetFakeCrosConfig(kTestPath, std::nullopt);
  EXPECT_EQ(cros_config_.CheckTrueCrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [true], but got "
            "[]");
}

TEST_F(CrosConfigTest, GetInteger) {
  SetFakeCrosConfig(kTestPath, std::nullopt);
  EXPECT_EQ(cros_config_.GetU8CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint8], but got "
            "[]");
  EXPECT_EQ(cros_config_.GetU32CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint32], but got "
            "[]");
  EXPECT_EQ(cros_config_.GetU64CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint64], but got "
            "[]");

  SetFakeCrosConfig(kTestPath, "not_int");
  EXPECT_EQ(cros_config_.GetU8CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint8], but got "
            "[not_int]");
  EXPECT_EQ(cros_config_.GetU32CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint32], but got "
            "[not_int]");
  EXPECT_EQ(cros_config_.GetU64CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint64], but got "
            "[not_int]");

  SetFakeCrosConfig(kTestPath, "-1");
  EXPECT_EQ(cros_config_.GetU8CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint8], but got "
            "[-1]");
  EXPECT_EQ(cros_config_.GetU32CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint32], but got "
            "[-1]");
  EXPECT_EQ(cros_config_.GetU64CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint64], but got "
            "[-1]");

  SetFakeCrosConfig(kTestPath, "0");
  EXPECT_EQ(cros_config_.GetU8CrosConfig(kTestPath).value(), 0);
  EXPECT_EQ(cros_config_.GetU32CrosConfig(kTestPath).value(), 0);
  EXPECT_EQ(cros_config_.GetU64CrosConfig(kTestPath).value(), 0);

  SetFakeCrosConfig(kTestPath, "256");
  EXPECT_EQ(cros_config_.GetU8CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint8], but got "
            "[256]");
  EXPECT_EQ(cros_config_.GetU32CrosConfig(kTestPath).value(), 256);
  EXPECT_EQ(cros_config_.GetU64CrosConfig(kTestPath).value(), 256);

  SetFakeCrosConfig(kTestPath, "4294967296");
  EXPECT_EQ(cros_config_.GetU8CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint8], but got "
            "[4294967296]");
  EXPECT_EQ(cros_config_.GetU32CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint32], but got "
            "[4294967296]");
  EXPECT_EQ(cros_config_.GetU64CrosConfig(kTestPath).value(), 4294967296);

  SetFakeCrosConfig(kTestPath, "18446744073709551616");
  EXPECT_EQ(cros_config_.GetU8CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint8], but got "
            "[18446744073709551616]");
  EXPECT_EQ(cros_config_.GetU32CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint32], but got "
            "[18446744073709551616]");
  EXPECT_EQ(cros_config_.GetU64CrosConfig(kTestPath).error(),
            "Expected cros_config property [a/b/c] to be [uint64], but got "
            "[18446744073709551616]");
}

}  // namespace
}  // namespace diagnostics
