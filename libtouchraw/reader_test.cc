// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtouchraw/reader.h"

#include <utility>

#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/mock_consumers.h"

namespace touchraw {

// Test buffer size.
constexpr int kTestSize = 10;

class ReaderTest : public testing::Test {
 protected:
  void SetUp() override {
    mock_consumer_ =
        std::make_unique<testing::StrictMock<MockHIDDataConsumer>>();
    reader_ =
        std::make_unique<Reader>(std::move(fd_), nullptr, mock_consumer_.get());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  std::unique_ptr<MockHIDDataConsumer> mock_consumer_;
  std::unique_ptr<Reader> reader_;
  // TODO: b/275615279 - Possible way to mock fd?
  base::ScopedFD fd_;  // Invalid file descriptor.
};

// TODO: b/275615279 - Add unit tests for Start function.
/* TEST_F(ReaderTest, StartSucceeded) {
  EXPECT_TRUE(reader_->Start().ok());
} */

TEST_F(ReaderTest, StopSucceeded) {
  reader_->Stop();
  EXPECT_EQ(reader_->watcher_, nullptr);
}

TEST_F(ReaderTest, ReadFailed) {
  reader_->OnFileCanReadWithoutBlocking(fd_.get());
  EXPECT_EQ(reader_->watcher_, nullptr);
}

TEST_F(ReaderTest, EmptyBuffer) {
  uint8_t buf[kTestSize] = {};
  reader_->ProcessData(buf, 0);
  task_environment_.RunUntilIdle();
}

TEST_F(ReaderTest, ValidBufferOneByte) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](const HIDData hd) {
        EXPECT_EQ(hd.report_id, 10);
        EXPECT_EQ(hd.payload.size(), 0);
      });

  uint8_t buf[kTestSize] = {10};
  reader_->ProcessData(buf, 1);
  task_environment_.RunUntilIdle();
}

TEST_F(ReaderTest, ValidBufferFiveBytes) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](const HIDData hd) {
        EXPECT_EQ(hd.report_id, 10);
        EXPECT_EQ(hd.payload.size(), 4);
        EXPECT_EQ(hd.payload[0], 11);
        EXPECT_EQ(hd.payload[1], 12);
        EXPECT_EQ(hd.payload[2], 13);
        EXPECT_EQ(hd.payload[3], 14);
      });

  uint8_t buf[kTestSize] = {10, 11, 12, 13, 14};
  reader_->ProcessData(buf, 5);
  task_environment_.RunUntilIdle();
}

}  // namespace touchraw
