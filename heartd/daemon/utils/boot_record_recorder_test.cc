// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/utils/boot_record_recorder.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include "heartd/daemon/database.h"

namespace heartd {

namespace {

constexpr char kFakeShutdownDirName[] = "shutdown.dir";
constexpr char kLatestBootID[] = "latest";
constexpr char kFakeBootIDContent[] =
    "2023-12-07T03:13:27.906000Z INFO boot_id: old_1"
    "2023-12-07T03:27:50.906000Z INFO boot_id: old_2"
    "2023-12-07T09:54:02.906000Z INFO boot_id: old_3"
    "2023-12-07T10:01:17.906000Z INFO boot_id: latest";

class BootMetricsRecorderTest : public testing::Test {
 public:
  BootMetricsRecorderTest() = default;
  ~BootMetricsRecorderTest() override = default;

  void Init() {
    db_path_ = temp_dir_.GetPath().Append("test_db.db");
    db_ = std::make_unique<Database>(db_path_.MaybeAsASCII());
    db_->Init();
    EXPECT_TRUE(db_->IsOpen());

    CreateFakeFiles();
  }

  void CreateFakeFiles() {
    CreateFakeShutdownMetricsDir();
    CreateFakeBootIDFile(kFakeBootIDContent);
  }

  void CreateFakeShutdownMetricsDir() {
    auto metrics_dir = GetFakeRoot().Append(kMetricsPath);
    auto shutdown_metrics_dir = metrics_dir.Append(kFakeShutdownDirName);
    EXPECT_TRUE(base::CreateDirectory(shutdown_metrics_dir));
  }

  void CreateFakeBootIDFile(const std::string& content) {
    auto boot_id_path = GetFakeRoot().Append(kBootIDPath);
    EXPECT_EQ(base::WriteFile(boot_id_path, content.c_str(), content.size()),
              content.size());
  }

  base::FilePath GetFakeRoot() { return temp_dir_.GetPath(); }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }
  void TearDown() override { ASSERT_TRUE(temp_dir_.Delete()); }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath db_path_;
  std::unique_ptr<Database> db_;
};

TEST_F(BootMetricsRecorderTest, RecordBootMetrics) {
  Init();
  RecordBootMetrics(GetFakeRoot(), db_.get());

  auto boot_records =
      db_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 2);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
  EXPECT_EQ(boot_records[1].id, kLatestBootID);
}

TEST_F(BootMetricsRecorderTest, NoShutdownMetricsIsFine) {
  Init();
  EXPECT_TRUE(
      brillo::DeletePathRecursively(GetFakeRoot().Append(kMetricsPath)));
  RecordBootMetrics(GetFakeRoot(), db_.get());

  auto boot_records =
      db_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kLatestBootID);
}

TEST_F(BootMetricsRecorderTest, NoBootIDIsFine) {
  Init();
  EXPECT_TRUE(brillo::DeleteFile(GetFakeRoot().Append(kBootIDPath)));
  RecordBootMetrics(GetFakeRoot(), db_.get());

  auto boot_records =
      db_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
}

TEST_F(BootMetricsRecorderTest, EmptyBootIDIsFine) {
  Init();
  EXPECT_TRUE(brillo::DeleteFile(GetFakeRoot().Append(kBootIDPath)));
  CreateFakeBootIDFile("");
  RecordBootMetrics(GetFakeRoot(), db_.get());

  auto boot_records =
      db_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
}

}  // namespace

}  // namespace heartd
