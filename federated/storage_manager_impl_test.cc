// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/storage_manager_impl.h"

#include <gmock/gmock.h>
#include <memory>

#include "federated/mock_example_database.h"

namespace federated {
namespace {

using testing::_;
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

TEST_F(StorageManagerImplTest, InsertExample) {
  EXPECT_CALL(*example_database_, IsOpen())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*example_database_, InsertExample(_))
      .WillRepeatedly(Return(true));

  // First call will fail due to the database !IsOpen;
  EXPECT_FALSE(storage_manager_impl_->OnExampleReceived("client", "example"));
  EXPECT_TRUE(storage_manager_impl_->OnExampleReceived("client", "example"));
}

TEST_F(StorageManagerImplTest, ExampleStreaming) {
  EXPECT_CALL(*example_database_, IsOpen())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*example_database_, PrepareStreamingForClient(_, _))
      .Times(2)
      .WillOnce(Return(false))
      .WillOnce(Return(true));

  // Return 10000 examples, then return base::nullopt.
  int streaming_example_count = 10000;
  ExampleRecord example_record;
  Expectation successful_get =
      EXPECT_CALL(*example_database_, GetNextStreamedRecord())
          .Times(streaming_example_count)
          .WillRepeatedly(
              Return(base::Optional<ExampleRecord>(example_record)));

  EXPECT_CALL(*example_database_, GetNextStreamedRecord())
      .After(successful_get)
      .WillOnce(Return(base::nullopt));

  EXPECT_CALL(*example_database_, CloseStreaming()).Times(1);
  EXPECT_CALL(*example_database_, DeleteExamplesWithSmallerIdForClient(_, _))
      .Times(1)
      .WillOnce(Return(true));

  // Fail due to !example_database_->IsOpen.
  EXPECT_FALSE(storage_manager_impl_->PrepareStreamingForClient("client"));

  // Fail due to !example_database_->PrepareStreamingForClient.
  EXPECT_FALSE(storage_manager_impl_->PrepareStreamingForClient("client"));

  ASSERT_TRUE(storage_manager_impl_->PrepareStreamingForClient("client"));
  int cnt = 0;
  std::string example;
  bool end_of_iterator = false;
  // Retrieve an example from example_database_, GetNextExample should always
  // return true because the example_database_ is valid. end_of_iterator = true
  // when example streaming reaches the end.
  while (true) {
    EXPECT_TRUE(
        storage_manager_impl_->GetNextExample(&example, &end_of_iterator));
    if (end_of_iterator)
      break;
    cnt++;
  }

  EXPECT_EQ(cnt, streaming_example_count);
  EXPECT_TRUE(storage_manager_impl_->CloseStreaming(true /* clean_examples */));
}

TEST_F(StorageManagerImplTest, ExampleStreamingWithSessionStopped) {
  EXPECT_CALL(*example_database_, IsOpen()).WillRepeatedly(Return(true));

  EXPECT_CALL(*example_database_, PrepareStreamingForClient(_, _))
      .WillRepeatedly(Return(true));
  ExampleRecord example_record;

  EXPECT_CALL(*example_database_, GetNextStreamedRecord())
      .WillRepeatedly(Return(base::Optional<ExampleRecord>(example_record)));

  ASSERT_TRUE(storage_manager_impl_->PrepareStreamingForClient("client"));
  int cnt = 100;
  std::string example;
  bool end_of_iterator = false;

  // The example streaming is healthy before we call OnSessionStopped.
  while (cnt > 0) {
    EXPECT_TRUE(
        storage_manager_impl_->GetNextExample(&example, &end_of_iterator));
    EXPECT_FALSE(end_of_iterator);
    cnt--;
  }
  storage_manager_impl_->OnSessionStopped();
  // After OnSessionStopped, GetNextExample returns false, which means the
  // example streaming stopped unexpectedly.
  EXPECT_FALSE(
      storage_manager_impl_->GetNextExample(&example, &end_of_iterator));
}

}  // namespace federated
