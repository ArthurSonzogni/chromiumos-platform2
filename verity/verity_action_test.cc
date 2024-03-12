// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Tests for verity::VerityAction

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/test/mock_log.h>
#include <gtest/gtest.h>

#include "verity/verity_action.h"

namespace verity {

using ::testing::_;
using ::testing::HasSubstr;

namespace {

constexpr DmVerityTable::RootDigestType kRootDigest = {
    '2', '1', 'f', '0', '2', '6', '8', 'f', '4', 'a', '2', '9', '3', 'd', '8',
    '1', '1', '0', '0', '7', '4', 'c', '6', '7', '8', 'a', '6', '5', '1', 'c',
    '6', '3', '8', 'd', '5', '6', 'a', '6', '1', '0', 'd', 'd', '2', '6', '6',
    '2', '9', '7', '5', 'a', '3', '5', 'd', '4', '5', '1', 'd', '3', '2', '5',
    '8', '0', '1', '8', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
};

constexpr DmVerityTable::SaltType kSalt = {
    'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2', '3', '4', '5', '6',
    '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd',
    'e', 'f', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '\0',
};

}  // namespace

class VerityActionTest : public ::testing::Test {
 public:
  VerityActionTest() = default;
  virtual ~VerityActionTest() = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(temp_dir_.IsValid());

    small_payload_file_path_ = base::FilePath(getenv("SRC"))
                                   .Append("test_data")
                                   .Append("small_payload.bin");
  }

  bool CreateFile(const base::FilePath& path, int64_t size) {
    base::File f(path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    if (!f.IsValid()) {
      LOG(ERROR) << "Failed to open path=" << path.value();
      return false;
    }
    if (!f.SetLength(size)) {
      LOG(ERROR) << "Failed to set length of path=" << path.value();
      return false;
    }
    return true;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath small_payload_file_path_;
};

TEST_F(VerityActionTest, PreVerify) {
  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  ASSERT_TRUE(CreateFile(payload_path, 4096 * 10));
  EXPECT_TRUE(DmVerityAction::PreVerify(
      payload_path, {"sha256",
                     kRootDigest,
                     kSalt,
                     {
                         .dev = "ROOT_DEV",
                         .block_count = 8,
                     },
                     {
                         .dev = "HASH_DEV",
                     },
                     DmVerityTable::HashPlacement::COLOCATED}));
}

TEST_F(VerityActionTest, PreVerifyEqualBlockSize) {
  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  ASSERT_TRUE(CreateFile(payload_path, 4096 * 10));
  EXPECT_FALSE(DmVerityAction::PreVerify(
      payload_path, {"sha256",
                     kRootDigest,
                     kSalt,
                     {
                         .dev = "ROOT_DEV",
                         .block_count = 10,
                     },
                     {
                         .dev = "HASH_DEV",
                     },
                     DmVerityTable::HashPlacement::COLOCATED}));
}

TEST_F(VerityActionTest, PreVerifyTableExceedsBlockSize) {
  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  ASSERT_TRUE(CreateFile(payload_path, 4096 * 10));
  EXPECT_FALSE(DmVerityAction::PreVerify(
      payload_path, {"sha256",
                     kRootDigest,
                     kSalt,
                     {
                         .dev = "ROOT_DEV",
                         .block_count = 11,
                     },
                     {
                         .dev = "HASH_DEV",
                     },
                     DmVerityTable::HashPlacement::COLOCATED}));
}

TEST_F(VerityActionTest, PreVerifyMissingPayload) {
  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  // Any dm-verity table should lead to failure.
  EXPECT_FALSE(DmVerityAction::PreVerify(
      payload_path, {"sha256",
                     kRootDigest,
                     kSalt,
                     {
                         .dev = "ROOT_DEV",
                         .block_count = 8,
                     },
                     {
                         .dev = "HASH_DEV",
                     },
                     DmVerityTable::HashPlacement::COLOCATED}));
}

TEST_F(VerityActionTest, TruncatePayloadToSource) {
  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  ASSERT_TRUE(CreateFile(payload_path, 4096 * 10));

  const base::FilePath source_img_path(
      temp_dir_.GetPath().Append("source.img"));
  std::unique_ptr<base::File> source_img_file;
  ASSERT_TRUE(DmVerityAction::TruncatePayloadToSource(
      payload_path, source_img_path,
      {"sha256",
       kRootDigest,
       kSalt,
       {
           .dev = "ROOT_DEV",
           .block_count = 8,
       },
       {
           .dev = "HASH_DEV",
       },
       DmVerityTable::HashPlacement::COLOCATED},
      &source_img_file));
  EXPECT_EQ(4096 * 8, source_img_file->GetLength());
}

TEST_F(VerityActionTest, TruncatePayloadToSourceMissingPayload) {
  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  const base::FilePath source_img_path(
      temp_dir_.GetPath().Append("source.img"));
  std::unique_ptr<base::File> source_img_file;
  EXPECT_FALSE(DmVerityAction::TruncatePayloadToSource(
      payload_path, source_img_path,
      {"sha256",
       kRootDigest,
       kSalt,
       {
           .dev = "ROOT_DEV",
           .block_count = 8,
       },
       {
           .dev = "HASH_DEV",
       },
       DmVerityTable::HashPlacement::COLOCATED},
      &source_img_file));
}

TEST_F(VerityActionTest, Verify) {
  EXPECT_EQ(0,
            DmVerityAction::Verify(small_payload_file_path_,
                                   {"sha256",
                                    kRootDigest,
                                    kSalt,
                                    {
                                        .dev = "ROOT_DEV",
                                        .block_count = 2,
                                    },
                                    {
                                        .dev = "HASH_DEV",
                                    },
                                    DmVerityTable::HashPlacement::COLOCATED}));
}

TEST_F(VerityActionTest, VerifyMissingPayload) {
  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  EXPECT_EQ(-1, DmVerityAction::Verify(
                    payload_path, {"sha256",
                                   kRootDigest,
                                   kSalt,
                                   {
                                       .dev = "ROOT_DEV",
                                       .block_count = 2,
                                   },
                                   {
                                       .dev = "HASH_DEV",
                                   },
                                   DmVerityTable::HashPlacement::COLOCATED}));
}

TEST_F(VerityActionTest, VerifyPartialHashtreeInPayload) {
  base::test::MockLog mock_log;
  mock_log.StartCapturingLogs();

  const base::FilePath payload_path(temp_dir_.GetPath().Append("payload.img"));
  ASSERT_TRUE(base::CopyFile(small_payload_file_path_, payload_path));

  base::File payload_file(payload_path,
                          base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  ASSERT_TRUE(payload_file.SetLength(payload_file.GetLength() - 16));

  EXPECT_CALL(mock_log, Log(_, _, _, _, _)).Times(testing::AnyNumber());
  EXPECT_CALL(mock_log, Log(::logging::LOGGING_ERROR, _, _, _,
                            HasSubstr("Final payload mismatch, did you forget "
                                      "to append the hashtree fully?")));
  EXPECT_EQ(-1, DmVerityAction::Verify(
                    payload_path, {"sha256",
                                   kRootDigest,
                                   kSalt,
                                   {
                                       .dev = "ROOT_DEV",
                                       .block_count = 2,
                                   },
                                   {
                                       .dev = "HASH_DEV",
                                   },
                                   DmVerityTable::HashPlacement::COLOCATED}));
  mock_log.StopCapturingLogs();
}

TEST_F(VerityActionTest, VerifyTableMismatch) {
  base::test::MockLog mock_log;
  mock_log.StartCapturingLogs();

  EXPECT_CALL(mock_log, Log(_, _, _, _, _)).Times(testing::AnyNumber());
  EXPECT_CALL(mock_log, Log(::logging::LOGGING_ERROR, _, _, _,
                            HasSubstr("Tables are not the same.")));
  EXPECT_EQ(-1,
            DmVerityAction::Verify(small_payload_file_path_,
                                   {"sha256",
                                    {},
                                    kSalt,
                                    {
                                        .dev = "ROOT_DEV",
                                        .block_count = 2,
                                    },
                                    {
                                        .dev = "HASH_DEV",
                                    },
                                    DmVerityTable::HashPlacement::COLOCATED}));
  mock_log.StopCapturingLogs();
}

}  // namespace verity
