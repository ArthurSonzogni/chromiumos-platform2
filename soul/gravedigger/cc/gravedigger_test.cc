// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "soul/gravedigger/cc/gravedigger.h"

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>
#include <string>

namespace gravedigger {

class GravediggerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    file_path_ = temp_dir_.GetPath().AppendASCII("test.txt");
    base::File file(file_path_, base::File::Flags::FLAG_CREATE_ALWAYS |
                                    base::File::Flags::FLAG_WRITE);
    ASSERT_TRUE(file.IsValid());

    data_ = "Hello World!";
    int bytes_written = file.WriteAtCurrentPos(data_.data(), data_.size());
    ASSERT_EQ(bytes_written, data_.size());
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath file_path_;
  std::string data_;
};

TEST_F(GravediggerTest, PathExists) {
  EXPECT_TRUE(LogFile::PathExists(base::FilePath("tests/test_log_file")));
  EXPECT_FALSE(LogFile::PathExists(base::FilePath("/tests/test_log_file")));
  EXPECT_TRUE(LogFile::PathExists(file_path_));

  EXPECT_EQ(LogFile::Open(base::FilePath("/tests/test_log_file")), nullptr);
  EXPECT_NE(LogFile::Open(base::FilePath("tests/test_log_file")), nullptr);
  EXPECT_NE(LogFile::Open(base::FilePath(file_path_)), nullptr);
}

TEST_F(GravediggerTest, Inode) {
  std::unique_ptr<LogFile> log_file = LogFile::Open(file_path_);
  EXPECT_NE(log_file->GetInode(), 0u);
}

TEST_F(GravediggerTest, ReadAtCurrentPosition) {
  std::unique_ptr<LogFile> log_file = LogFile::Open(file_path_);

  char buf[12];
  base::expected<int64_t, int64_t> read_bytes =
      log_file->ReadAtCurrentPosition(buf, data_.size());
  ASSERT_TRUE(read_bytes.has_value());
  EXPECT_EQ(*read_bytes, data_.size());
  EXPECT_EQ(std::string(buf, data_.size()), data_);
}

TEST_F(GravediggerTest, ReadAtCurrentPositionBigBuf) {
  std::unique_ptr<LogFile> log_file = LogFile::Open(file_path_);

  char buf[1200];
  base::expected<int64_t, int64_t> read_bytes =
      log_file->ReadAtCurrentPosition(buf, 1200);
  ASSERT_TRUE(read_bytes.has_value());
  EXPECT_EQ(*read_bytes, data_.size());
  EXPECT_EQ(std::string(buf, data_.size()), data_);
}

TEST_F(GravediggerTest, ReadAtCurrentPositionVector) {
  std::unique_ptr<LogFile> log_file = LogFile::Open(file_path_);

  std::vector<unsigned char> buf(data_.size());
  base::expected<int64_t, int64_t> read_bytes =
      log_file->ReadAtCurrentPosition(buf);
  ASSERT_TRUE(read_bytes.has_value());
  EXPECT_EQ(*read_bytes, data_.size());
  EXPECT_EQ(std::string(buf.begin(), buf.end()), data_);
}

TEST_F(GravediggerTest, SeekToBegin) {
  std::unique_ptr<LogFile> log_file = LogFile::Open(file_path_);

  std::vector<unsigned char> data(data_.size());
  ASSERT_EQ(*log_file->ReadAtCurrentPosition(data), data_.size());
  ASSERT_EQ(*log_file->ReadAtCurrentPosition(data), 0);
  EXPECT_TRUE(log_file->SeekToBegin());
  EXPECT_EQ(*log_file->ReadAtCurrentPosition(data), data_.size());
}

TEST_F(GravediggerTest, SeekToEnd) {
  std::unique_ptr<LogFile> log_file = LogFile::Open(file_path_);

  EXPECT_TRUE(log_file->SeekToEnd());

  std::vector<unsigned char> data(50);
  EXPECT_EQ(*log_file->ReadAtCurrentPosition(data), 0);
}

TEST_F(GravediggerTest, SeekBeforeEnd) {
  std::unique_ptr<LogFile> log_file = LogFile::Open(file_path_);

  EXPECT_TRUE(log_file->SeekBeforeEnd());

  std::vector<unsigned char> data(6);
  EXPECT_EQ(*log_file->ReadAtCurrentPosition(data), 1);
}

}  // namespace gravedigger
