// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/storage_manager_impl.h"

#include <memory>
#include <utility>

#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>

#include "federated/mock_example_database.h"
#include "federated/test_utils.h"
#include "federated/utils.h"

namespace federated {
namespace {

using testing::_;
using testing::ByMove;
using testing::Expectation;
using testing::ExpectationSet;
using testing::Mock;
using testing::Return;
using testing::StrictMock;

}  // namespace

class StorageManagerImplTest : public testing::Test {
 public:
  StorageManagerImplTest()
      : example_database_(
            new StrictMock<MockExampleDatabase>(base::FilePath(""))),
        storage_manager_impl_(new StorageManagerImpl()) {}

  void SetUp() override {
    storage_manager_impl_->set_example_database_for_testing(example_database_);
  }

  void TearDown() override {
    storage_manager_impl_.reset();
    Mock::VerifyAndClearExpectations(example_database_);
  }

 protected:
  MockExampleDatabase* example_database_;
  std::unique_ptr<StorageManagerImpl> storage_manager_impl_;
};

TEST_F(StorageManagerImplTest, ExampleRecieved) {
  EXPECT_CALL(*example_database_, IsOpen())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*example_database_, InsertExample("client", _))
      .WillRepeatedly(Return(true));

  // First call will fail due to the database !IsOpen;
  EXPECT_FALSE(storage_manager_impl_->OnExampleReceived("client", "example"));
  EXPECT_TRUE(storage_manager_impl_->OnExampleReceived("client", "example"));
}

// Test that the databases example iterator is faithfully returned.
TEST_F(StorageManagerImplTest, ExampleStreaming) {
  EXPECT_CALL(*example_database_, IsOpen())
      .WillOnce(Return(false))
      .WillOnce(Return(true));

  EXPECT_CALL(*example_database_, ExampleCount("fake_client"))
      .WillOnce(Return(kMinExampleCount));

  // Return a valid number of examples, then return absl::OutOfRangeError().
  auto db_and_it = MockExampleDatabase::FakeIterator(kMinExampleCount);
  EXPECT_CALL(*example_database_, GetIterator("fake_client"))
      .WillOnce(Return(ByMove(std::move(std::get<1>(db_and_it)))));

  // Fail due to !example_database_->IsOpen.
  EXPECT_EQ(storage_manager_impl_->GetExampleIterator("fake_client"),
            base::nullopt);
  base::Optional<ExampleDatabase::Iterator> it =
      storage_manager_impl_->GetExampleIterator("fake_client");
  ASSERT_TRUE(it.has_value());

  // Expect the examples we specified.
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

// Test that minimum example limit is honored.
TEST_F(StorageManagerImplTest, ExampleStreamingMinimum) {
  EXPECT_CALL(*example_database_, IsOpen()).WillOnce(Return(true));

  EXPECT_CALL(*example_database_, ExampleCount("fake_client"))
      .WillOnce(Return(kMinExampleCount - 1));

  EXPECT_EQ(storage_manager_impl_->GetExampleIterator("fake_client"),
            base::nullopt);
}

}  // namespace federated
