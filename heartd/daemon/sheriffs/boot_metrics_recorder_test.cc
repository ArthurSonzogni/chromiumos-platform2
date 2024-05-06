// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/sheriffs/boot_metrics_recorder.h"

#include <memory>
#include <string>
#include <utility>
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
    "2023-12-07T03:13:27.906000Z INFO boot_id: old_1\n"
    "2023-12-07T03:27:50.906000Z INFO boot_id: old_2\n"
    "2023-12-07T09:54:02.906000Z INFO boot_id: old_3\n"
    "2023-12-07T10:01:17.906000Z INFO boot_id: latest\n";

class BootMetricsRecorderTest : public testing::Test {
 public:
  BootMetricsRecorderTest() = default;
  ~BootMetricsRecorderTest() override = default;

  void Init() {
    database_ = std::make_unique<Database>(
        temp_dir_.GetPath().Append("test_db.db").MaybeAsASCII());
    boot_metrics_recorder_ =
        std::make_unique<BootMetricsRecorder>(GetRoot(), database_.get());

    database_->Init();
    EXPECT_TRUE(database_->IsOpen());
    CreateFakeFiles();
  }

  void CreateFakeFiles() {
    CreateFakeShutdownMetricsDir();
    CreateFakeBootIDFile(kFakeBootIDContent);
  }

  void CreateFakeShutdownMetricsDir() {
    auto metrics_dir = GetRoot().Append(kMetricsPath);
    auto shutdown_metrics_dir = metrics_dir.Append(kFakeShutdownDirName);
    EXPECT_TRUE(base::CreateDirectory(shutdown_metrics_dir));
  }

  void CreateFakeBootIDFile(const std::string& content) {
    auto boot_id_path = GetRoot().Append(kBootIDPath);
    EXPECT_EQ(base::WriteFile(boot_id_path, content.c_str(), content.size()),
              content.size());
  }

  base::FilePath GetRoot() { return temp_dir_.GetPath(); }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }
  void TearDown() override { ASSERT_TRUE(temp_dir_.Delete()); }

 protected:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<Database> database_;
  std::unique_ptr<BootMetricsRecorder> boot_metrics_recorder_;
};

TEST_F(BootMetricsRecorderTest, RecordBootMetrics) {
  Init();
  boot_metrics_recorder_->OneShotWork();

  auto boot_records =
      database_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 2);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
  EXPECT_EQ(boot_records[1].id, kLatestBootID);
}

TEST_F(BootMetricsRecorderTest, NoShutdownMetricsIsFine) {
  Init();
  EXPECT_TRUE(brillo::DeletePathRecursively(GetRoot().Append(kMetricsPath)));
  boot_metrics_recorder_->OneShotWork();

  auto boot_records =
      database_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kLatestBootID);
}

TEST_F(BootMetricsRecorderTest, NoBootIDIsFine) {
  Init();
  EXPECT_TRUE(brillo::DeleteFile(GetRoot().Append(kBootIDPath)));
  boot_metrics_recorder_->OneShotWork();

  auto boot_records =
      database_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
}

TEST_F(BootMetricsRecorderTest, EmptyBootIDIsFine) {
  Init();
  EXPECT_TRUE(brillo::DeleteFile(GetRoot().Append(kBootIDPath)));
  CreateFakeBootIDFile("");
  boot_metrics_recorder_->OneShotWork();

  auto boot_records =
      database_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
}

TEST_F(BootMetricsRecorderTest, WrongBootIDText) {
  Init();
  EXPECT_TRUE(brillo::DeleteFile(GetRoot().Append(kBootIDPath)));
  CreateFakeBootIDFile("2024-01-01 INFO wrong_boot_id_text: test");
  boot_metrics_recorder_->OneShotWork();

  auto boot_records =
      database_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
}

TEST_F(BootMetricsRecorderTest, WrongBootIDTokenLength) {
  Init();
  EXPECT_TRUE(brillo::DeleteFile(GetRoot().Append(kBootIDPath)));
  CreateFakeBootIDFile("2024-01-01 INFO boot_id: one_more_text test");
  boot_metrics_recorder_->OneShotWork();

  auto boot_records =
      database_->GetBootRecordFromTime(base::Time().Now() - base::Minutes(1));
  EXPECT_EQ(boot_records.size(), 1);
  EXPECT_EQ(boot_records[0].id, kFakeShutdownDirName);
}

}  // namespace

}  // namespace heartd
