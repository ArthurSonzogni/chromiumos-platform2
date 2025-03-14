// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/embedding_database.h"

#include <memory>
#include <optional>
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
#include <metrics/metrics_library_mock.h>

#include "odml/coral/common.h"
#include "odml/i18n/language_detector.h"

namespace coral {

namespace {

using TextLanguage = ::on_device_model::LanguageDetector::TextLanguage;
using ::testing::ElementsAre;
using ::testing::NiceMock;
using ::testing::Optional;

MATCHER_P(HasEmbedding, matcher, "") {
  return ExplainMatchResult(matcher, arg.embedding, result_listener);
}

MATCHER_P(HasSafetyVerdict, matcher, "") {
  return ExplainMatchResult(matcher, arg.safety_verdict, result_listener);
}

MATCHER_P(HasLanguageDetectionResult, matcher, "") {
  return ExplainMatchResult(matcher, arg.languages, result_listener);
}

}  // namespace

class EmbeddingDatabaseTest : public testing::Test {
 public:
  EmbeddingDatabaseTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME),
        coral_metrics_(raw_ref(metrics_)) {}

  void SetUp() override {
    CHECK(database_file_.Create());
    file_path_ = database_file_.path();
  }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_;

  NiceMock<MetricsLibraryMock> metrics_;
  CoralMetrics coral_metrics_;

  base::ScopedTempFile database_file_;
  base::FilePath file_path_;

  EmbeddingDatabaseFactory factory_;
};

TEST_F(EmbeddingDatabaseTest, InMemoryWriteRead) {
  // Empty database.
  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key2"), EmbeddingEntry());

  database->Put("key1",
                EmbeddingEntry{.embedding{1, 2, 3}, .safety_verdict = true});
  database->Put("key2",
                EmbeddingEntry{.embedding{4, 5, 6}, .safety_verdict = false});
  database->Put("key3", EmbeddingEntry{{7, 8, 9}});

  // key4 doesn't exist.
  EXPECT_THAT(database->Get("key1"), HasEmbedding(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key1"), HasSafetyVerdict(Optional(true)));
  EXPECT_THAT(database->Get("key3"), HasEmbedding(ElementsAre(7, 8, 9)));
  EXPECT_THAT(database->Get("key3"), HasSafetyVerdict(std::nullopt));
  EXPECT_THAT(database->Get("key3"), HasLanguageDetectionResult(std::nullopt));
  EXPECT_THAT(database->Get("key4"), EmbeddingEntry());

  // key1 and key 3 are overwritten.
  database->Put("key1", EmbeddingEntry{{10, 20, 30}});
  database->Put(
      "key3", EmbeddingEntry{.embedding{70, 80, 90},
                             .safety_verdict = false,
                             .languages = LanguageDetectionResult{TextLanguage{
                                 .locale = "en", .confidence = 0.8}}});
  // key4 is inserted.
  database->Put(
      "key4", EmbeddingEntry{.embedding{10, 11, 12}, .safety_verdict = false});

  EXPECT_THAT(database->Get("key1"), HasEmbedding(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key1"), HasSafetyVerdict(std::nullopt));
  EXPECT_THAT(database->Get("key3"), HasEmbedding(ElementsAre(70, 80, 90)));
  EXPECT_THAT(database->Get("key3"), HasSafetyVerdict(Optional(false)));
  EXPECT_THAT(database->Get("key3"),
              HasLanguageDetectionResult(LanguageDetectionResult{
                  TextLanguage{.locale = "en", .confidence = 0.8}}));
  EXPECT_THAT(database->Get("key4"), HasEmbedding(ElementsAre(10, 11, 12)));
  EXPECT_THAT(database->Get("key4"), HasSafetyVerdict(Optional(false)));
}

TEST_F(EmbeddingDatabaseTest, WriteThenRead) {
  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  database->Put("key1",
                EmbeddingEntry{.embedding{1, 2, 3}, .safety_verdict = true});
  database->Put("key2", EmbeddingEntry{{4, 5, 6}});
  database->Put(
      "key3", EmbeddingEntry{.embedding = {7, 8, 9},
                             .languages = LanguageDetectionResult{TextLanguage{
                                 .locale = "en", .confidence = 0.8}}});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), HasEmbedding(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key1"), HasSafetyVerdict(Optional(true)));
  EXPECT_THAT(database->Get("key1"), HasLanguageDetectionResult(std::nullopt));
  EXPECT_THAT(database->Get("key3"), HasEmbedding(ElementsAre(7, 8, 9)));
  EXPECT_THAT(database->Get("key3"),
              HasLanguageDetectionResult(LanguageDetectionResult{
                  TextLanguage{.locale = "en", .confidence = 0.8}}));
  EXPECT_THAT(database->Get("key4"), HasSafetyVerdict(std::nullopt));
}

TEST_F(EmbeddingDatabaseTest, RecordsPruned) {
  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  // When the 1001st entry is inserted, the first 100 entries will be pruned.
  for (int i = 0; i < 1100; i++) {
    database->Put(std::string("key") + std::to_string(i),
                  EmbeddingEntry{{1, 2, 3}});
    FastForwardBy(base::Seconds(1));
  }
  for (int i = 0; i < 1100; i++) {
    std::string key = std::string("key") + std::to_string(i);
    if (i < 100) {
      EXPECT_EQ(database->Get(key), EmbeddingEntry());
    } else {
      EXPECT_THAT(database->Get(key), HasEmbedding(ElementsAre(1, 2, 3)));
    }
  }
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  // The result should be the same.
  for (int i = 0; i < 1100; i++) {
    std::string key = std::string("key") + std::to_string(i);
    if (i < 100) {
      EXPECT_EQ(database->Get(key), EmbeddingEntry());
    } else {
      EXPECT_THAT(database->Get(key), HasEmbedding(ElementsAre(1, 2, 3)));
    }
  }
}

TEST_F(EmbeddingDatabaseTest, RecordsExpire) {
  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(10));
  // timestamp 0.
  database->Put("key1", EmbeddingEntry{{1, 2, 3}});
  database->Put("key2", EmbeddingEntry{{4, 5, 6}});
  database->Put("key3", EmbeddingEntry{{7, 8, 9}});

  // timestamp 1.
  FastForwardBy(base::Seconds(1));
  database->Put("key4", EmbeddingEntry{{10, 20, 30}});
  database->Put("key5", EmbeddingEntry{{40, 50, 60}});

  // timestamp 6.
  FastForwardBy(base::Seconds(5));
  database->Get({"key2"});

  // timestamp 11.
  // key1 and key 3 have timestamp 0 so expired.
  // key2 is alive because it is accessed at timestamp 6.
  FastForwardBy(base::Seconds(5));
  database->Sync();

  // key2, key4, and key5 has timestamp 11 now.
  EXPECT_THAT(database->Get("key1"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key2"), HasEmbedding(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key4"), HasEmbedding(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), HasEmbedding(ElementsAre(40, 50, 60)));
  database.reset();

  // timestamp 11.
  // Reads it back from the file with no ttl set.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key2"), HasEmbedding(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key4"), HasEmbedding(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), HasEmbedding(ElementsAre(40, 50, 60)));
  // No records are removed since no ttl was set.
  database.reset();

  // timestamp 11.
  // Reads it back again from the file.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(30));
  EXPECT_THAT(database->Get("key1"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key2"), HasEmbedding(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key4"), HasEmbedding(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), HasEmbedding(ElementsAre(40, 50, 60)));
  // No records are removed since the ttl is long.
  database.reset();

  // timestamp 11.
  // Reads it back again from the file.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(3));
  EXPECT_THAT(database->Get("key1"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key2"), HasEmbedding(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key4"), HasEmbedding(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), HasEmbedding(ElementsAre(40, 50, 60)));
  // timestamp 13.
  FastForwardBy(base::Seconds(2));
  database->Put("key3", EmbeddingEntry{{50, 51, 52}});
  // timestamp 15.
  FastForwardBy(base::Seconds(2));
  database->Get({"key4"});
  // key2: timestamp 11
  // key3: timestamp 13
  // key4: timestamp 15
  // key5: timestamp 11
  // When syncing, records with timestamp < 12 (key2, key5) are removed.
  database.reset();

  // timestamp 17.
  FastForwardBy(base::Seconds(3));
  // Reads it back again from the file. Records with timestamp < 14 (key4)
  // are expired. But since we don't remove stale records when loading the file,
  // it is kept.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(5));
  // Now key3, key4 has timestamp 17.
  EXPECT_THAT(database->Get("key1"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key2"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key3"), HasEmbedding(ElementsAre(50, 51, 52)));
  EXPECT_THAT(database->Get("key4"), HasEmbedding(ElementsAre(10, 20, 30)));
  EXPECT_THAT(database->Get("key5"), EmbeddingEntry());
  // timestamp 23.
  FastForwardBy(base::Seconds(6));
  // Records with timestamp < 18 (key3, key4) are removed.
  database.reset();

  // timestamp 23.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key2"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key3"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key4"), EmbeddingEntry());
  EXPECT_THAT(database->Get("key5"), EmbeddingEntry());
}

TEST_F(EmbeddingDatabaseTest, FileNotExist) {
  // Remove the temp file.
  ASSERT_TRUE(database_file_.Delete());

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  database->Put("key1", EmbeddingEntry{{1, 2, 3}});
  database->Put("key2", EmbeddingEntry{{4, 5, 6}});
  database->Put("key3", EmbeddingEntry{{7, 8, 9}});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), HasEmbedding(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key2"), HasEmbedding(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), HasEmbedding(ElementsAre(7, 8, 9)));
}

TEST_F(EmbeddingDatabaseTest, FileCorrupted) {
  // Write something invalid.
  std::string buf = "corrupted";
  ASSERT_TRUE(base::WriteFile(file_path_, buf));

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  database->Put("key1", EmbeddingEntry{{1, 2, 3}});
  database->Put("key2", EmbeddingEntry{{4, 5, 6}});
  database->Put("key3", EmbeddingEntry{{7, 8, 9}});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path_, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), HasEmbedding(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key2"), HasEmbedding(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), HasEmbedding(ElementsAre(7, 8, 9)));
}

TEST_F(EmbeddingDatabaseTest, TestCreateDirectory) {
  base::ScopedTempDir tmp_dir;
  ASSERT_TRUE(tmp_dir.CreateUniqueTempDir());
  base::FilePath dir_path = tmp_dir.GetPath();
  base::FilePath file_path = dir_path.Append("sub_dir").Append("database");

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path, base::Seconds(0));
  database->Put("key1", EmbeddingEntry{{1, 2, 3}});
  database->Put("key2", EmbeddingEntry{{4, 5, 6}});
  database->Put("key3", EmbeddingEntry{{7, 8, 9}});
  // Synced to file in destructor.
  database.reset();

  // Reads it back from the file.
  database =
      factory_.Create(raw_ref(coral_metrics_), file_path, base::Seconds(0));
  EXPECT_THAT(database->Get("key1"), HasEmbedding(ElementsAre(1, 2, 3)));
  EXPECT_THAT(database->Get("key2"), HasEmbedding(ElementsAre(4, 5, 6)));
  EXPECT_THAT(database->Get("key3"), HasEmbedding(ElementsAre(7, 8, 9)));
}

TEST_F(EmbeddingDatabaseTest, TestCreateDirectoryFailure) {
  // We should not have permission to create a directory at the root directory.
  base::FilePath dir_path("/");
  base::FilePath file_path = dir_path.Append("sub_dir").Append("database");

  std::unique_ptr<EmbeddingDatabaseInterface> database =
      factory_.Create(raw_ref(coral_metrics_), file_path, base::Seconds(0));
  EXPECT_EQ(database.get(), nullptr);
}

}  // namespace coral
