// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/prefs.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/files/scoped_temp_dir.h>

using base::FilePath;
using std::string;

namespace modemfwd {

namespace {
const char key1[] = "key1";
const char key2[] = "key2";
const char value1[] = "value1";
const char value2[] = "#$&*^!(($))";
const char sub_prefs_name[] = "subpref";
};  // namespace

class PrefsTest : public testing::Test {
 protected:
  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());
    prefs_ = Prefs::CreatePrefs(temp_dir_.GetPath());
    CHECK(prefs_ != nullptr);
  }

 protected:
  std::unique_ptr<Prefs> prefs_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(PrefsTest, SetAndGetkey) {
  EXPECT_TRUE(prefs_->SetKey(key1, value1));
  EXPECT_TRUE(prefs_->Exists(key1));
  EXPECT_TRUE(prefs_->KeyValueMatches(key1, value1));

  string actual_value;
  EXPECT_TRUE(prefs_->GetKey(key1, &actual_value));
  EXPECT_EQ(value1, actual_value);
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(key1)));

  EXPECT_TRUE(prefs_->SetKey(key2, value2));
  EXPECT_TRUE(prefs_->Exists(key2));
  EXPECT_TRUE(prefs_->KeyValueMatches(key2, value2));
  EXPECT_TRUE(prefs_->GetKey(key2, &actual_value));
  EXPECT_EQ(value2, actual_value);
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(key2)));
}

TEST_F(PrefsTest, RepeatedSet) {
  EXPECT_TRUE(prefs_->SetKey(key1, value1));
  EXPECT_TRUE(prefs_->KeyValueMatches(key1, value1));

  string actual_value;
  EXPECT_TRUE(prefs_->GetKey(key1, &actual_value));
  EXPECT_EQ(value1, actual_value);
  EXPECT_TRUE(prefs_->SetKey(key1, value2));
  EXPECT_TRUE(prefs_->GetKey(key1, &actual_value));
  EXPECT_EQ(value2, actual_value);
  EXPECT_TRUE(prefs_->KeyValueMatches(key1, value2));

  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(key1)));
}

TEST_F(PrefsTest, Createkey) {
  EXPECT_TRUE(prefs_->Create(key1));
  EXPECT_TRUE(prefs_->Exists(key1));
  string actual_value;
  EXPECT_TRUE(prefs_->GetKey(key1, &actual_value));
  EXPECT_EQ("", actual_value);
}

TEST_F(PrefsTest, CreateSubPrefs) {
  auto sub_prefs = Prefs::CreatePrefs(*prefs_, sub_prefs_name);
  EXPECT_TRUE(sub_prefs != nullptr);
  EXPECT_TRUE(base::PathExists(sub_prefs->GetPrefRootPath()));
  EXPECT_EQ(sub_prefs->GetPrefRootPath(),
            prefs_->GetPrefRootPath().Append(sub_prefs_name));
}

TEST_F(PrefsTest, SubPrefsSetGetCreateExistkey) {
  const auto sub_prefs_path = temp_dir_.GetPath().Append(sub_prefs_name);
  auto sub_prefs = Prefs::CreatePrefs(*prefs_, sub_prefs_name);

  EXPECT_TRUE(sub_prefs->Create(key1));
  EXPECT_TRUE(sub_prefs->Exists(key1));
  EXPECT_TRUE(base::PathExists(sub_prefs_path.Append(key1)));

  EXPECT_TRUE(sub_prefs->SetKey(key1, value1));
  EXPECT_TRUE(sub_prefs->Exists(key1));
  string actual_value;
  EXPECT_TRUE(sub_prefs->GetKey(key1, &actual_value));
  EXPECT_EQ(value1, actual_value);
  EXPECT_TRUE(base::PathExists(sub_prefs_path.Append(key1)));

  EXPECT_TRUE(sub_prefs->SetKey(key2, value2));
  EXPECT_TRUE(sub_prefs->Exists(key2));
  EXPECT_TRUE(sub_prefs->GetKey(key2, &actual_value));
  EXPECT_EQ(value2, actual_value);
  EXPECT_TRUE(base::PathExists(sub_prefs_path.Append(key2)));
}

}  // namespace modemfwd
