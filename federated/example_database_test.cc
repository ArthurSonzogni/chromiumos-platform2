// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/example_database.h"

#include <string>
#include <unordered_set>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <base/optional.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include "federated/example_database_test_utils.h"
#include "federated/utils.h"

namespace federated {
namespace {
const std::unordered_set<std::string> kTestClients = {"test_client_1",
                                                      "test_client_2"};
}  // namespace

class ExampleDatabaseTest : public testing::Test {
 public:
  ExampleDatabaseTest(const ExampleDatabaseTest&) = delete;
  ExampleDatabaseTest& operator=(const ExampleDatabaseTest&) = delete;

  ExampleDatabase* get_db() const { return db_; }
  const base::FilePath& temp_path() const { return temp_dir_.GetPath(); }

  // Prepares a database, table test_client_1 has 100 examples (id from 1
  // to 100), table test_client_2 is created by db_->Init() and is empty.
  bool CreateExampleDatabaseAndInitialize() {
    const base::FilePath db_path =
        temp_dir_.GetPath().Append(kDatabaseFileName);
    if (CreateDatabaseForTesting(db_path) != SQLITE_OK) {
      LOG(ERROR) << "Failed to create database file.";
      return false;
    }

    db_ = new ExampleDatabase(db_path, kTestClients);
    if (!db_->Init() || !db_->IsOpen() || !db_->CheckIntegrity()) {
      LOG(ERROR) << "Failed to initialize or check integrity of db_.";
      return false;
    }

    return true;
  }

 protected:
  ExampleDatabaseTest() = default;

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }
  void TearDown() override {
    ASSERT_TRUE(temp_dir_.Delete());
    if (db_)
      delete db_;
  }

 private:
  base::ScopedTempDir temp_dir_;
  ExampleDatabase* db_ = nullptr;
};

// This test runs the same steps as CreateExampleDatabaseAndInitialize, but
// checks step by step.
TEST_F(ExampleDatabaseTest, CreateDatabase) {
  // Prepares a database file.
  base::FilePath db_path = temp_path().Append(kDatabaseFileName);
  ASSERT_EQ(CreateDatabaseForTesting(db_path), SQLITE_OK);
  EXPECT_TRUE(base::PathExists(db_path));

  // Creates the db instance.
  ExampleDatabase db(db_path, kTestClients);

  // Initializes the db and checks integrity.
  EXPECT_TRUE(db.Init());
  EXPECT_TRUE(db.IsOpen());
  EXPECT_TRUE(db.CheckIntegrity());

  // Closes it.
  EXPECT_TRUE(db.Close());
}

TEST_F(ExampleDatabaseTest, DatabaseQueryFromNonEmptyTable) {
  ASSERT_TRUE(CreateExampleDatabaseAndInitialize());
  ExampleDatabase& db = *get_db();

  std::string client_name = "test_client_1";
  // Table test_client_1 has 100 records, limit=50 can return 50 examples.
  int limit = 50;
  EXPECT_TRUE(db.PrepareStreamingForClient(client_name, limit));
  int64_t count = 0;
  while (true) {  // id from 1 to 50.
    auto maybe_record = db.GetNextStreamedRecord();
    if (maybe_record == base::nullopt)
      break;
    EXPECT_EQ(maybe_record.value().id, count + 1);
    EXPECT_EQ(maybe_record.value().serialized_example,
              base::StringPrintf("example_%zu", count + 1));
    count++;
  }
  EXPECT_EQ(count, 50);
  db.CloseStreaming();

  // Limit=150 only returns 100 examples, that's all in the table.
  limit = 150;
  EXPECT_TRUE(db.PrepareStreamingForClient(client_name, limit));
  count = 0;
  while (true) {  // id from 1 to 100.
    auto maybe_record = db.GetNextStreamedRecord();
    if (maybe_record == base::nullopt)
      break;
    EXPECT_EQ(maybe_record.value().id, count + 1);
    EXPECT_EQ(maybe_record.value().serialized_example,
              base::StringPrintf("example_%zu", count + 1));
    count++;
  }
  EXPECT_EQ(count, 100);
  db.CloseStreaming();

  EXPECT_TRUE(db.Close());
}

TEST_F(ExampleDatabaseTest, PrepareStreamingFailure) {
  ASSERT_TRUE(CreateExampleDatabaseAndInitialize());
  ExampleDatabase& db = *get_db();

  int limit = 100;
  // Table test_client_2 is empty, returns false on PrepareStreamingForClient
  std::string client_name = "test_client_2";
  EXPECT_FALSE(db.PrepareStreamingForClient(client_name, limit));

  // Table test_client_3 doesn't exist, returns false on
  // PrepareStreamingForClient.
  client_name = "test_client_3";
  EXPECT_FALSE(db.PrepareStreamingForClient(client_name, limit));

  EXPECT_TRUE(db.Close());
}

// Test that example_database can handle some illegal query operations.
TEST_F(ExampleDatabaseTest, UnexpectedQuery) {
  ASSERT_TRUE(CreateExampleDatabaseAndInitialize());
  ExampleDatabase& db = *get_db();

  // Insert an example to table test_client_2.
  ExampleRecord record = {-1 /*placeholder for id*/, "test_client_2",
                          "manually_inserted_example", base::Time::Now()};
  EXPECT_TRUE(db.InsertExample(record));

  std::string client_name = "test_client_1";

  // Calls GetNextStreamedRecord without PrepareStreamingForClient, gets
  // base::nullopt.
  EXPECT_EQ(db.GetNextStreamedRecord(), base::nullopt);

  // Calls GetNextStreamedRecord after CloseStreaming, gets base::nullopt.
  EXPECT_TRUE(db.PrepareStreamingForClient(client_name, 10));
  db.CloseStreaming();
  EXPECT_EQ(db.GetNextStreamedRecord(), base::nullopt);

  // Calls GetNextStreamedRecord after it already returned base::nullopt.
  EXPECT_TRUE(db.PrepareStreamingForClient(client_name, 10));
  int count = 0;
  while (db.GetNextStreamedRecord() != base::nullopt)
    count++;

  EXPECT_EQ(count, 10);
  EXPECT_EQ(db.GetNextStreamedRecord(), base::nullopt);
  db.CloseStreaming();

  // A subsequent PrepareStreamingForClient call before CloseStreaming() will
  // fail and have no influnce on the existing streaming.
  EXPECT_TRUE(db.PrepareStreamingForClient("test_client_1", 10));
  count = 0;
  for (size_t i = 0; i < 5; i++) {
    EXPECT_NE(db.GetNextStreamedRecord(), base::nullopt);
    count++;
  }

  EXPECT_FALSE(db.PrepareStreamingForClient("test_client_2", 50));

  while (db.GetNextStreamedRecord() != base::nullopt)
    count++;

  EXPECT_EQ(count, 10);

  // Subsequent PrepareStreamingForClient fails as long as the previous
  // streaming is not closed by CloseStreaming, even if it already hit the end.
  EXPECT_FALSE(db.PrepareStreamingForClient("test_client_2", 50));
  db.CloseStreaming();
  EXPECT_TRUE(db.PrepareStreamingForClient("test_client_2", 50));
}

TEST_F(ExampleDatabaseTest, InsertExample) {
  ASSERT_TRUE(CreateExampleDatabaseAndInitialize());
  ExampleDatabase& db = *get_db();

  ExampleRecord record;
  record.serialized_example = "manually_inserted_example";
  record.timestamp = base::Time::Now();

  // Before inserting, table test_client_2 is empty, returns false on
  // PrepareStreamingForClient.
  std::string client_name = "test_client_2";
  record.client_name = client_name;

  EXPECT_FALSE(db.PrepareStreamingForClient(client_name, 100));

  EXPECT_TRUE(db.InsertExample(record));

  // After inserting, GetNextStreamedRecord will succeed for 1 time.
  EXPECT_TRUE(db.PrepareStreamingForClient(client_name, 100));
  int64_t count = 0;
  while (db.GetNextStreamedRecord() != base::nullopt)
    count++;

  EXPECT_EQ(count, 1);
  db.CloseStreaming();

  // Fails to insert into a non-existing table;
  client_name = "test_client_3";
  record.client_name = client_name;

  EXPECT_FALSE(db.InsertExample(record));

  EXPECT_TRUE(db.Close());
}

TEST_F(ExampleDatabaseTest, DeleteExamples) {
  ASSERT_TRUE(CreateExampleDatabaseAndInitialize());
  ExampleDatabase& db = *get_db();

  std::string client_name = "test_client_1";
  // Delete examples with id <= 30 from table test_client_1;
  EXPECT_TRUE(db.DeleteExamplesWithSmallerIdForClient(client_name, 30));

  EXPECT_TRUE(db.PrepareStreamingForClient(client_name, 100));
  int64_t count = 0;
  while (true) {  // id from 31 to 100.
    auto maybe_record = db.GetNextStreamedRecord();
    if (maybe_record == base::nullopt)
      break;
    EXPECT_EQ(maybe_record.value().id, count + 31);
    EXPECT_EQ(maybe_record.value().serialized_example,
              base::StringPrintf("example_%zu", count + 31));
    count++;
  }
  EXPECT_EQ(count, 70);
  db.CloseStreaming();

  // No examples with id <= 20 now, returns false;
  EXPECT_FALSE(db.DeleteExamplesWithSmallerIdForClient(client_name, 20));

  client_name = "test_client_2";
  // Delete examples from an empty table, returns false;
  EXPECT_FALSE(db.DeleteExamplesWithSmallerIdForClient(client_name, 100));

  client_name = "test_client_3";
  // Delete examples from a non-existing table, returns false;
  EXPECT_FALSE(db.DeleteExamplesWithSmallerIdForClient(client_name, 100));

  EXPECT_TRUE(db.Close());
}

}  // namespace federated
