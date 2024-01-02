// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/database.h"

#include <string>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include "heartd/daemon/boot_record.h"

namespace heartd {

namespace {

void VerifyEqualBootRecord(const BootRecord& record1,
                           const BootRecord& record2) {
  EXPECT_EQ(record1.id, record2.id);
  EXPECT_EQ(record1.time, record2.time);
}

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
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir temp_dir_;
  base::FilePath db_path_;
  std::unique_ptr<Database> db_;
};

TEST_F(DatabaseTest, CreateDatabase) {
  Init();
  EXPECT_TRUE(db_->TableExists(kBootRecordTable));
}

TEST_F(DatabaseTest, InsertBootRecord) {
  Init();
  BootRecord boot_record1{"id1", base::Time().Now()};
  BootRecord boot_record2{"id2", base::Time().Now()};
  db_->InsertBootRecord(boot_record1);
  db_->InsertBootRecord(boot_record2);

  auto boot_records = db_->GetBootRecordFromTime(base::Time().Now());
  EXPECT_EQ(boot_records.size(), 2);
  VerifyEqualBootRecord(boot_record1, boot_records[0]);
  VerifyEqualBootRecord(boot_record2, boot_records[1]);
}

TEST_F(DatabaseTest, InsertBootRecordVerifyTimeFilter) {
  Init();
  BootRecord boot_record1{"id1", base::Time().Now()};
  db_->InsertBootRecord(boot_record1);

  task_environment_.FastForwardBy(base::Seconds(10));
  BootRecord boot_record2{"id2", base::Time().Now()};
  db_->InsertBootRecord(boot_record2);

  auto boot_records =
      db_->GetBootRecordFromTime(base::Time().Now() - base::Seconds(5));
  EXPECT_EQ(boot_records.size(), 1);
  VerifyEqualBootRecord(boot_record2, boot_records[0]);
}

}  // namespace

}  // namespace heartd
