// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/embedding_database.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/files/scoped_temp_file.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "coral/proto_bindings/embedding.pb.h"

namespace coral {

namespace {

using ::testing::ElementsAre;
using ::testing::Optional;

}  // namespace

class EmbeddingDatabaseTest : public testing::Test {
 public:
  EmbeddingDatabaseTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    CHECK(database_file_.Create());
    file_path_ = database_file_.path();
  }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

 protected:
  base::ScopedTempFile database_file_;
  base::FilePath file_path_;

  EmbeddingDatabaseFactory factory_;

  base::test::SingleThreadTaskEnvironment task_environment_;
};

TEST_F(EmbeddingDatabaseTest, InMemoryWriteRead) {
  // Empty database.
  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), std::nullopt);
  EXPECT_THAT(database->Get("key2"), std::nullopt);

  database->Put("key1", {1, 2, 3});
  database->Put("key2", {4, 5, 6});
  database->Put("key3", {7, 8, 9});

  // key4 doesn't exist.
  EXPECT_THAT(database->Get("key1"), Optional(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key3"), Optional(ElementsAre(7, 8, 9)));
  EXPECT_THAT(database->Get("key4"), std::nullopt);

  // key1 is overwritten.
  database->Put("key1", {10, 20, 30});
  // key4 is inserted.
  database->Put("key4", {10, 11, 12});

  EXPECT_THAT(database->Get("key1"), Optional(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key4"), Optional(ElementsAre(10, 11, 12)));
}

TEST_F(EmbeddingDatabaseTest, WriteThenRead) {
  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(file_path_, base::Seconds(0));
  database->Put("key1", {1, 2, 3});
  database->Put("key2", {4, 5, 6});
  database->Put("key3", {7, 8, 9});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database = factory_.Create(file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), Optional(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key3"), Optional(ElementsAre(7, 8, 9)));
}

TEST_F(EmbeddingDatabaseTest, RecordsExpire) {
  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(file_path_, base::Seconds(10));
  // timestamp 0.
  database->Put("key1", {1, 2, 3});
  database->Put("key2", {4, 5, 6});
  database->Put("key3", {7, 8, 9});

  // timestamp 1.
  FastForwardBy(base::Seconds(1));
  database->Put("key4", {10, 20, 30});
  database->Put("key5", {40, 50, 60});

  // timestamp 6.
  FastForwardBy(base::Seconds(5));
  database->Get({"key2"});

  // timestamp 11.
  // key1 and key 3 have timestamp 0 so expired.
  // key2 is alive because it is accessed at timestamp 6.
  FastForwardBy(base::Seconds(5));
  database->Sync();

  // key2, key4, and key5 has timestamp 11 now.
  EXPECT_THAT(database->Get("key1"), std::nullopt);
  EXPECT_THAT(database->Get("key2"), Optional(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), std::nullopt);
  EXPECT_THAT(database->Get("key4"), Optional(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), Optional(ElementsAre(40, 50, 60)));
  database.reset();

  // Reads it back from the file with no ttl set.
  database = factory_.Create(file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), std::nullopt);
  EXPECT_THAT(database->Get("key2"), Optional(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), std::nullopt);
  EXPECT_THAT(database->Get("key4"), Optional(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), Optional(ElementsAre(40, 50, 60)));
  database.reset();

  // Reads it back again from the file with some long ttl.
  database = factory_.Create(file_path_, base::Seconds(30));
  EXPECT_THAT(database->Get("key1"), std::nullopt);
  EXPECT_THAT(database->Get("key2"), Optional(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), std::nullopt);
  EXPECT_THAT(database->Get("key4"), Optional(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), Optional(ElementsAre(40, 50, 60)));

  // timestamp 13.
  FastForwardBy(base::Seconds(2));
  // key3 has timestamp 13 now, while key2 and key4 remain 11.
  database->Put("key3", {50, 51, 52});
  // timestamp 15.
  FastForwardBy(base::Seconds(2));
  // key4 has timestamp 15.
  database->Get({"key4"});
  database.reset();

  // timestamp 17.
  FastForwardBy(base::Seconds(2));
  // Reads it back again from the file with some short ttl set.
  // key2 and key5 have timestamp 11 so expired. key3 has timestamp 13,
  // key4 has timestamp 15, so kept alive.
  database = factory_.Create(file_path_, base::Seconds(5));
  // Now key3 and key4 has timestamp 17.
  EXPECT_THAT(database->Get("key1"), std::nullopt);
  EXPECT_THAT(database->Get("key2"), std::nullopt);
  EXPECT_THAT(database->Get("key3"), Optional(ElementsAre(50, 51, 52)));
  EXPECT_THAT(database->Get("key4"), Optional(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), std::nullopt);
  database.reset();

  // timestamp 19.
  FastForwardBy(base::Seconds(2));
  // Reads it one last time from the file.
  // key3 and key4 has timestamp 17, so not expired.
  database = factory_.Create(file_path_, base::Seconds(5));
  EXPECT_THAT(database->Get("key1"), std::nullopt);
  EXPECT_THAT(database->Get("key2"), std::nullopt);
  EXPECT_THAT(database->Get("key3"), Optional(ElementsAre(50, 51, 52)));
  EXPECT_THAT(database->Get("key4"), Optional(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), std::nullopt);
}

TEST_F(EmbeddingDatabaseTest, FileNotExist) {
  // Remove the temp file.
  ASSERT_TRUE(database_file_.Delete());

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(file_path_, base::Seconds(0));
  database->Put("key1", {1, 2, 3});
  database->Put("key2", {4, 5, 6});
  database->Put("key3", {7, 8, 9});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database = factory_.Create(file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), Optional(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key2"), Optional(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), Optional(ElementsAre(7, 8, 9)));
}

TEST_F(EmbeddingDatabaseTest, FileCorrupted) {
  // Write something invalid.
  std::string buf = "corrupted";
  ASSERT_TRUE(base::WriteFile(file_path_, buf));

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(file_path_, base::Seconds(0));
  database->Put("key1", {1, 2, 3});
  database->Put("key2", {4, 5, 6});
  database->Put("key3", {7, 8, 9});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database = factory_.Create(file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), Optional(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key2"), Optional(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), Optional(ElementsAre(7, 8, 9)));
}

TEST_F(EmbeddingDatabaseTest, TestCreateDirectory) {
  base::ScopedTempDir tmp_dir;
  ASSERT_TRUE(tmp_dir.CreateUniqueTempDir());
  base::FilePath dir_path = tmp_dir.GetPath();
  base::FilePath file_path = dir_path.Append("sub_dir").Append("database");

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(file_path, base::Seconds(0));
  database->Put("key1", {1, 2, 3});
  database->Put("key2", {4, 5, 6});
  database->Put("key3", {7, 8, 9});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database = factory_.Create(file_path, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), Optional(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key2"), Optional(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), Optional(ElementsAre(7, 8, 9)));
}

TEST_F(EmbeddingDatabaseTest, TestCreateDirectoryFailure) {
  // We should not have permission to create a directory at the root directory.
  base::FilePath dir_path("/");
  base::FilePath file_path = dir_path.Append("sub_dir").Append("database");

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(file_path, base::Seconds(0));
  EXPECT_EQ(database.get(), nullptr);
}

}  // namespace coral
