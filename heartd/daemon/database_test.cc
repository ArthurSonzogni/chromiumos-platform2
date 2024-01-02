// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/database.h"

#include <string>

#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

namespace heartd {

namespace {

class DatabaseTest : public testing::Test {
 public:
  DatabaseTest() = default;
  ~DatabaseTest() override = default;

  void Init() {
    db_path_ = temp_dir_.GetPath().Append("test_db.db");
    db_ = std::make_unique<Database>(db_path_.MaybeAsASCII());
    db_->Init();

    EXPECT_TRUE(db_->IsOpen());
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }
  void TearDown() override { ASSERT_TRUE(temp_dir_.Delete()); }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath db_path_;
  std::unique_ptr<Database> db_;
};

TEST_F(DatabaseTest, CreateDatabase) {
  Init();
  EXPECT_TRUE(db_->TableExists(kBootRecordTable));
}

}  // namespace

}  // namespace heartd
