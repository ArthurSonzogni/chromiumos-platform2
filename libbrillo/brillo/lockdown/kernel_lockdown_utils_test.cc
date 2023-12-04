// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/lockdown/kernel_lockdown_utils.h>

#include <optional>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace {

class KernelLockdownUtilTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    kernel_lockdown_ = temp_dir_.GetPath().Append("klockdown");
  }
  base::FilePath kernel_lockdown_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(KernelLockdownUtilTest, DisabledMode) {
  ASSERT_TRUE(
      base::WriteFile(kernel_lockdown_, "[none] integrity confidentiality"));
  EXPECT_EQ(brillo::GetLockdownMode(kernel_lockdown_),
            brillo::KernelLockdownMode::kDisabled);
}

TEST_F(KernelLockdownUtilTest, IntegrityMode) {
  ASSERT_TRUE(
      base::WriteFile(kernel_lockdown_, "none [integrity] confidentiality"));
  EXPECT_EQ(brillo::GetLockdownMode(kernel_lockdown_),
            brillo::KernelLockdownMode::kIntegrity);
}

TEST_F(KernelLockdownUtilTest, ConfidentialityMode) {
  ASSERT_TRUE(
      base::WriteFile(kernel_lockdown_, "none integrity [confidentiality]"));
  EXPECT_EQ(brillo::GetLockdownMode(kernel_lockdown_),
            brillo::KernelLockdownMode::kConfidentiality);
}

TEST_F(KernelLockdownUtilTest, FileNotExist) {
  ASSERT_FALSE(PathExists(kernel_lockdown_));
  EXPECT_EQ(brillo::GetLockdownMode(kernel_lockdown_), std::nullopt);
}

struct KernelLockdownTestCase {
  std::string test_name;
  std::string bad_input;
};

class KernelLockdownUtilParamTest
    : public KernelLockdownUtilTest,
      public testing::WithParamInterface<KernelLockdownTestCase> {};

TEST_P(KernelLockdownUtilParamTest, ReturnsNullOptIfInputInvalid) {
  const KernelLockdownTestCase& test_case = GetParam();
  ASSERT_TRUE(base::WriteFile(kernel_lockdown_, test_case.bad_input));
  EXPECT_EQ(brillo::GetLockdownMode(kernel_lockdown_), std::nullopt);
}

INSTANTIATE_TEST_SUITE_P(
    KernelLockdownUtilParamTest,
    KernelLockdownUtilParamTest,
    testing::ValuesIn<KernelLockdownTestCase>({
        {"MissingBracket1", "[none integrity confidentiality"},
        {"MissingBracket2", "]none integrity confidentiality"},
        {"EmptyMode", "[]none integrity confidentiality"},
        {"BadMode", "[none integrity] confidentiality"},
        {"NoBrackets", "none integrity confidentiality"},
        {"EOF", "none integrity confidentiality["},
        {"EmptyFile", ""},
    }),
    [](const testing::TestParamInfo<KernelLockdownUtilParamTest::ParamType>&
           info) { return info.param.test_name; });

}  // namespace
