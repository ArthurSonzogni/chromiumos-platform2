// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/storage_manager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <google/protobuf/util/time_util.h>

#include "federated/mock_example_database.h"
#include "federated/protos/cros_example_selector_criteria.pb.h"
#include "federated/test_utils.h"
#include "federated/utils.h"

namespace federated {
namespace {

using ::google::protobuf::util::TimeUtil;
using ::testing::_;
using ::testing::ByMove;
using ::testing::Expectation;
using ::testing::ExpectationSet;
using ::testing::Mock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

}  // namespace

class StorageManagerTest : public Test {
 public:
  StorageManagerTest()
      : example_database_(
            new StrictMock<MockExampleDatabase>(base::FilePath(""))),
        storage_manager_(new StorageManager()),
        default_start_time_(SecondsAfterEpoch(1)),
        default_end_time_(SecondsAfterEpoch(100)) {
    // criteria_ is initialized so that the time range can cover all examples in
    // the test example database. Modify it if a particular range or a malformed
    // criteria are required.
    *criteria_.mutable_start_time() =
        TimeUtil::MillisecondsToTimestamp(default_start_time_.ToJavaTime());
    *criteria_.mutable_end_time() =
        TimeUtil::MillisecondsToTimestamp(default_end_time_.ToJavaTime());
  }

  void SetUp() override {
    storage_manager_->set_example_database_for_testing(example_database_);
  }

  void TearDown() override {
    Mock::VerifyAndClearExpectations(example_database_);
  }

 protected:
  MockExampleDatabase* const example_database_;
  const std::unique_ptr<StorageManager> storage_manager_;
  const base::Time default_start_time_;
  const base::Time default_end_time_;
  fcp::client::CrosExampleSelectorCriteria criteria_;
};

TEST_F(StorageManagerTest, ExampleRecieved) {
  EXPECT_CALL(*example_database_, IsOpen())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*example_database_, InsertExample("client", _))
      .WillRepeatedly(Return(true));

  // First call will fail due to the database !IsOpen;
  EXPECT_FALSE(storage_manager_->OnExampleReceived("client", "example"));
  EXPECT_TRUE(storage_manager_->OnExampleReceived("client", "example"));
}

// Tests that the databases example iterator is faithfully returned.
TEST_F(StorageManagerTest, ExampleStreaming) {
  EXPECT_CALL(*example_database_, IsOpen())
      .WillOnce(Return(false))
      .WillOnce(Return(true));

  EXPECT_CALL(
      *example_database_,
      ExampleCount("fake_client", default_start_time_, default_end_time_))
      .WillOnce(Return(kMinExampleCount));

  // Returns a valid number of examples, then returns absl::OutOfRangeError().
  auto db_and_it = MockExampleDatabase::FakeIterator(kMinExampleCount);
  EXPECT_CALL(
      *example_database_,
      GetIterator("fake_client", default_start_time_, default_end_time_))
      .WillOnce(Return(ByMove(std::move(std::get<1>(db_and_it)))));

  // Fails due to !example_database_->IsOpen.
  EXPECT_EQ(storage_manager_->GetExampleIterator("fake_client", criteria_),
            std::nullopt);
  std::optional<ExampleDatabase::Iterator> it =
      storage_manager_->GetExampleIterator("fake_client", criteria_);
  ASSERT_TRUE(it.has_value());

  // Expects the examples we specified.
  int count = 0;
  while (true) {
    const absl::StatusOr<ExampleRecord> record = it->Next();
    if (!record.ok()) {
      EXPECT_TRUE(absl::IsOutOfRange(record.status()));
      break;
    }

    EXPECT_EQ(record->id, count + 1);
    EXPECT_EQ(record->serialized_example,
              base::StringPrintf("example_%d", count + 1));
    EXPECT_EQ(record->timestamp, SecondsAfterEpoch(count + 1));

    ++count;
  }

  EXPECT_EQ(count, kMinExampleCount);
}

// Tests that minimum example limit is honored.
TEST_F(StorageManagerTest, ExampleStreamingMinimum) {
  EXPECT_CALL(*example_database_, IsOpen()).WillOnce(Return(true));

  EXPECT_CALL(
      *example_database_,
      ExampleCount("fake_client", default_start_time_, default_end_time_))
      .WillOnce(Return(kMinExampleCount - 1));

  EXPECT_EQ(storage_manager_->GetExampleIterator("fake_client", criteria_),
            std::nullopt);
}

// Tests GetExampleIterator returns nullopt when criteria proto is malformed.
TEST_F(StorageManagerTest, ExampleStreamingFailureWithMalformedCriteria) {
  EXPECT_CALL(*example_database_, IsOpen()).WillOnce(Return(true));
  // Makes criteria has start_time > end_time.
  *criteria_.mutable_start_time() = TimeUtil::MillisecondsToTimestamp(2000LL);
  *criteria_.mutable_end_time() = TimeUtil::MillisecondsToTimestamp(1000LL);
  EXPECT_EQ(storage_manager_->GetExampleIterator("fake_client", criteria_),
            std::nullopt);
}

// Tests that storage_manager is able to get example iterator with valid
// criteria (currently only a time range).
TEST_F(StorageManagerTest, ExampleStreamingWithCriteria) {
  EXPECT_CALL(*example_database_, IsOpen()).WillOnce(Return(true));

  int example_count = 100;
  base::Time start_time = SecondsAfterEpoch(11);
  base::Time end_time = SecondsAfterEpoch(30);

  EXPECT_CALL(*example_database_,
              ExampleCount("fake_client", start_time, end_time))
      .WillOnce(Return(example_count));

  // Returns a valid number of examples within the time range, then returns
  // absl::OutOfRangeError().
  auto db_and_it =
      MockExampleDatabase::FakeIterator(example_count, start_time, end_time);
  EXPECT_CALL(*example_database_,
              GetIterator("fake_client", start_time, end_time))
      .WillOnce(Return(ByMove(std::move(std::get<1>(db_and_it)))));

  // Sets the criteria properly.
  *criteria_.mutable_start_time() =
      TimeUtil::MillisecondsToTimestamp(start_time.ToJavaTime());
  *criteria_.mutable_end_time() =
      TimeUtil::MillisecondsToTimestamp(end_time.ToJavaTime());
  std::optional<ExampleDatabase::Iterator> it =
      storage_manager_->GetExampleIterator("fake_client", criteria_);
  ASSERT_TRUE(it.has_value());

  // Expects the examples we specified.
  int count = 0;
  int expected_id = 11;
  while (true) {
    const absl::StatusOr<ExampleRecord> record = it->Next();
    if (!record.ok()) {
      EXPECT_TRUE(absl::IsOutOfRange(record.status()));
      break;
    }

    EXPECT_EQ(record->id, expected_id);
    EXPECT_EQ(record->serialized_example,
              base::StringPrintf("example_%d", expected_id));
    EXPECT_EQ(record->timestamp, SecondsAfterEpoch(expected_id));

    ++count;
    ++expected_id;
  }

  EXPECT_EQ(count, 20);
}

}  // namespace federated
