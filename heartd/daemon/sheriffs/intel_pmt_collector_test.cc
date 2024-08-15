// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/sheriffs/intel_pmt_collector.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <base/values.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <gtest/gtest.h>

namespace heartd {

namespace {

class IntelPMTCollectorTest : public testing::Test {
 public:
  IntelPMTCollectorTest() = default;
  ~IntelPMTCollectorTest() override = default;

  void Init() {
    fake_snapshot_ = new pmt::Snapshot();
    fake_collector_ = new pmt::PmtCollector();
    intel_pmt_collector_ = std::make_unique<IntelPMTCollector>(
        GetRoot(), fake_collector_, fake_snapshot_);
    // Arbitrarily set a field to make it non-empty.
    fake_snapshot_->set_timestamp(10);
  }

  base::FilePath GetRoot() { return temp_dir_.GetPath(); }

  void CreateConfig(const base::Value::Dict& config) {
    std::string json;
    base::JSONWriter::WriteWithOptions(
        config, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
    EXPECT_TRUE(base::WriteFile(GetRoot().Append(kIntelPMTConfigPath), json));
  }

  void CreateLogHeader(int snapshot_size) {
    auto log_path = GetRoot().Append(kIntelPMTLogPath);
    int fd = open(log_path.MaybeAsASCII().c_str(), O_RDWR | O_CREAT, 0664);

    pmt::LogHeader header;
    header.set_snapshot_size(snapshot_size);
    header.SerializeToFileDescriptor(fd);
  }

  int GetSnapshotSizeInLogHeader() {
    auto log_path = GetRoot().Append(kIntelPMTLogPath);
    int fd = open(log_path.MaybeAsASCII().c_str(), O_RDWR | O_CREAT, 0664);

    auto fis = std::make_unique<google::protobuf::io::FileInputStream>(fd);
    auto is =
        std::make_unique<google::protobuf::io::CodedInputStream>(fis.get());

    pmt::LogHeader header;
    // We have to set this otherwise `ByteSizeLong()` returns 0.
    header.set_snapshot_size(1);
    is->PushLimit(header.ByteSizeLong());
    EXPECT_TRUE(header.ParseFromCodedStream(is.get()));

    return header.snapshot_size();
  }

  int ReadCounter() {
    auto counter_path = GetRoot().Append(kIntelPMTCounterPath);
    std::string content;
    int counter = -1;

    EXPECT_TRUE(ReadFileToString(counter_path, &content));
    EXPECT_TRUE(base::StringToInt(content, &counter));

    return counter;
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    auto pmt_dir = GetRoot().Append("var/lib/heartd/intel_pmt/");
    ASSERT_TRUE(base::CreateDirectory(pmt_dir));
  }
  void TearDown() override { ASSERT_TRUE(temp_dir_.Delete()); }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<IntelPMTCollector> intel_pmt_collector_;
  pmt::LogHeader header_;
  pmt::Snapshot* fake_snapshot_;
  pmt::PmtCollector* fake_collector_;
};

TEST_F(IntelPMTCollectorTest, NoConfig) {
  Init();

  EXPECT_FALSE(intel_pmt_collector_->HasShiftWork());
}

TEST_F(IntelPMTCollectorTest, EmptyConfigShouldWork) {
  base::Value::Dict config;
  CreateConfig(config);
  Init();

  EXPECT_TRUE(intel_pmt_collector_->HasShiftWork());
}

TEST_F(IntelPMTCollectorTest, WrongHeaderWillBeFixed) {
  base::Value::Dict config;
  CreateConfig(config);
  Init();

  // Create a header with wrong snapshot size.
  int wrong_snapshot_size = fake_snapshot_->ByteSizeLong() + 999;
  CreateLogHeader(wrong_snapshot_size);
  EXPECT_EQ(GetSnapshotSizeInLogHeader(), wrong_snapshot_size);

  // `OneShotWork()` will check the header and fixed it.
  intel_pmt_collector_->OneShotWork();
  EXPECT_EQ(GetSnapshotSizeInLogHeader(), fake_snapshot_->ByteSizeLong());
}

TEST_F(IntelPMTCollectorTest, DefaultEmptyLog) {
  base::Value::Dict config;
  CreateConfig(config);
  Init();

  // `OneShotWork()` will create the log header if it's empty.
  intel_pmt_collector_->OneShotWork();
  EXPECT_EQ(GetSnapshotSizeInLogHeader(), fake_snapshot_->ByteSizeLong());
}

TEST_F(IntelPMTCollectorTest, VerifyCounterAndSnapshotContent) {
  base::Value::Dict config;
  CreateConfig(config);
  Init();

  // `OneShotWork()` will create the log header if it's empty.
  intel_pmt_collector_->OneShotWork();
  EXPECT_EQ(GetSnapshotSizeInLogHeader(), fake_snapshot_->ByteSizeLong());

  // Store three times.
  int count = 3;
  for (int i = 1; i <= count; ++i) {
    fake_snapshot_->set_timestamp(i);
    intel_pmt_collector_->WriteSnapshot();
  }

  // Verify counter.
  EXPECT_EQ(ReadCounter(), count);

  // Verify snapshot content.
  auto log_path = GetRoot().Append(kIntelPMTLogPath);
  int fd = open(log_path.MaybeAsASCII().c_str(), O_RDWR | O_CREAT, 0664);

  auto fis = std::make_unique<google::protobuf::io::FileInputStream>(fd);
  auto is = std::make_unique<google::protobuf::io::CodedInputStream>(fis.get());

  pmt::LogHeader header;
  // We have to set this otherwise `ByteSizeLong()` returns 0.
  header.set_snapshot_size(1);
  auto limit = is->PushLimit(header.ByteSizeLong());
  EXPECT_TRUE(header.ParseFromCodedStream(is.get()));
  is->PopLimit(limit);

  size_t snapshot_size = header.snapshot_size();
  EXPECT_EQ(snapshot_size, 9);
  for (int i = 1; i <= count; ++i) {
    limit = is->PushLimit(snapshot_size);
    pmt::Snapshot snapshot;
    EXPECT_TRUE(snapshot.ParseFromCodedStream(is.get()));
    EXPECT_EQ(snapshot.timestamp(), i);
    is->PopLimit(limit);
  }
}

TEST_F(IntelPMTCollectorTest, CleanUpInNextBootUp) {
  // Create the log file manually.
  auto log_path = GetRoot().Append(kIntelPMTLogPath);
  EXPECT_TRUE(base::WriteFile(log_path, ""));
  EXPECT_TRUE(base::PathExists(log_path));

  // No config.
  Init();

  EXPECT_FALSE(intel_pmt_collector_->HasShiftWork());

  // Clean up.
  intel_pmt_collector_->CleanUp();

  EXPECT_FALSE(base::PathExists(log_path));
}

TEST_F(IntelPMTCollectorTest, NoCleanUpWithinSameLifeCycle) {
  base::Value::Dict config;
  CreateConfig(config);
  Init();

  // `OneShotWork()` generates the log.
  intel_pmt_collector_->OneShotWork();
  auto log_path = GetRoot().Append(kIntelPMTLogPath);
  EXPECT_TRUE(base::PathExists(log_path));

  // Clean up shouldn't work at this moment.
  intel_pmt_collector_->CleanUp();
  EXPECT_TRUE(base::PathExists(log_path));

  // Delete config file.
  EXPECT_TRUE(brillo::DeleteFile(GetRoot().Append(kIntelPMTConfigPath)));

  // Clean up shouldn't work at this moment as well.
  intel_pmt_collector_->CleanUp();
  EXPECT_TRUE(base::PathExists(log_path));
}

}  // namespace

}  // namespace heartd
