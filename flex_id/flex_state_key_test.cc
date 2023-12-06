// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_id/flex_state_key.h"

#include <optional>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

namespace flex_id {

namespace {

constexpr char kGoodFlexStateKey[] = "a_good_flex_state_key";
constexpr char kPreservedFlexStateKey[] =
    "an_oldie_but_a_goodie_flex_state_key";

}  // namespace

class FlexStateKeyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();
    flex_state_key_generator_ = flex_id::FlexStateKeyGenerator(test_path_);
  }

  void CreatePreservedFlexStateKey() {
    base::FilePath preserved_flex_state_key_path =
        test_path_.Append("mnt/stateful_partition/unencrypted/preserve/flex");
    CHECK(base::CreateDirectory(preserved_flex_state_key_path));
    CHECK(
        base::WriteFile(preserved_flex_state_key_path.Append("flex_state_key"),
                        kPreservedFlexStateKey));
  }

  void CreateFlexStateKey(const std::string& flex_state_key) {
    base::FilePath flex_state_key_path = test_path_.Append("var/lib/flex_id");
    CHECK(base::CreateDirectory(flex_state_key_path));
    CHECK(base::WriteFile(flex_state_key_path.Append("flex_state_key"),
                          flex_state_key));
  }

  void DeleteFlexStateKey() {
    base::FilePath flex_state_key_path =
        test_path_.Append("var/lib/flex_id/flex_state_key");
    CHECK(brillo::DeleteFile(flex_state_key_path));
  }

  std::optional<flex_id::FlexStateKeyGenerator> flex_state_key_generator_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(FlexStateKeyTest, PreservedFlexStateKey) {
  EXPECT_FALSE(flex_state_key_generator_->TryPreservedFlexStateKey());

  CreatePreservedFlexStateKey();
  EXPECT_EQ(flex_state_key_generator_->TryPreservedFlexStateKey(),
            kPreservedFlexStateKey);
}

TEST_F(FlexStateKeyTest, FlexStateKey) {
  // No flex_state_key should return false.
  DeleteFlexStateKey();
  EXPECT_FALSE(flex_state_key_generator_->ReadFlexStateKey());

  // A blank flex_state_key should return false.
  DeleteFlexStateKey();
  CreateFlexStateKey("");
  EXPECT_FALSE(flex_state_key_generator_->ReadFlexStateKey());

  // A valid flex_state_key should be used if present.
  DeleteFlexStateKey();
  CreateFlexStateKey(kGoodFlexStateKey);
  EXPECT_EQ(flex_state_key_generator_->ReadFlexStateKey(), kGoodFlexStateKey);
}

TEST_F(FlexStateKeyTest, GenerateAndSaveFlexStateKey) {
  // A new flex_state_key should be generated.
  DeleteFlexStateKey();
  EXPECT_TRUE(flex_state_key_generator_->GenerateAndSaveFlexStateKey());
  EXPECT_EQ(
      flex_state_key_generator_->GenerateAndSaveFlexStateKey().value().length(),
      128);

  // A preserved flex_state_key should be used if present.
  DeleteFlexStateKey();
  CreatePreservedFlexStateKey();
  EXPECT_EQ(flex_state_key_generator_->GenerateAndSaveFlexStateKey(),
            kPreservedFlexStateKey);

  // An existing flex_state_key should be used if present.
  DeleteFlexStateKey();
  CreateFlexStateKey(kGoodFlexStateKey);
  EXPECT_EQ(flex_state_key_generator_->GenerateAndSaveFlexStateKey(),
            kGoodFlexStateKey);
}

}  // namespace flex_id
