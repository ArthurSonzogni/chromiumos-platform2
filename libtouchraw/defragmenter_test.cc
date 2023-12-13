// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/defragmenter.h"
#include "libtouchraw/mock_consumers.h"

namespace touchraw {

// TODO: b/275615279 - Improve unit tests of libtouchraw.

class DefragmenterTest : public testing::Test {
 protected:
  void SetUp() override {
    auto mock_consumer =
        std::make_unique<testing::StrictMock<MockHeatmapConsumer>>();
    mock_consumer_ = mock_consumer.get();
    defrag_ = std::make_unique<Defragmenter>(std::move(mock_consumer));

    chunk1_ = std::make_unique<HeatmapChunk>(HeatmapChunk{
        0,                     // Vendor id.
        1,                     // Protocol version.
        2,                     // Scan time.
        std::nullopt,          // Byte count.
        std::nullopt,          // Sequence ID.
        ReportType::kInvalid,  // Report type.
        {},                    // Payload.
    });
    chunk2_ = std::make_unique<HeatmapChunk>(HeatmapChunk{
        0,                     // Vendor id.
        1,                     // Protocol version.
        2,                     // Scan time.
        std::nullopt,          // Byte count.
        std::nullopt,          // Sequence ID.
        ReportType::kInvalid,  // Report type.
        {},                    // Payload.
    });
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  MockHeatmapConsumer* mock_consumer_;
  std::unique_ptr<Defragmenter> defrag_;
  std::unique_ptr<HeatmapChunk> chunk1_;
  std::unique_ptr<HeatmapChunk> chunk2_;
};

TEST_F(DefragmenterTest, SingleValidChunk) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> hm) {
        EXPECT_EQ(hm->vendor_id, 0);
        EXPECT_EQ(hm->protocol_version, 1);
        EXPECT_EQ(hm->scan_time, 2);
        EXPECT_EQ(hm->encoding, 3);
        EXPECT_EQ(hm->bit_depth, 4);
        EXPECT_EQ(hm->height, 5);
        EXPECT_EQ(hm->width, 6);
        EXPECT_EQ(hm->threshold, 2055);
        EXPECT_EQ(hm->length, 2);
        EXPECT_EQ(hm->payload.size(), 2);
        EXPECT_EQ(hm->payload[0], 10);
        EXPECT_EQ(hm->payload[1], 11);
      });

  chunk1_->byte_count = 10;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 2, 0, 10, 11};
  defrag_->Push(std::move(chunk1_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, TwoSingleValidChunks) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .Times(2)
      .WillOnce([&](std::unique_ptr<const Heatmap> hm) {
        EXPECT_EQ(hm->vendor_id, 0);
        EXPECT_EQ(hm->protocol_version, 1);
        EXPECT_EQ(hm->scan_time, 2);
        EXPECT_EQ(hm->encoding, 3);
        EXPECT_EQ(hm->bit_depth, 4);
        EXPECT_EQ(hm->height, 5);
        EXPECT_EQ(hm->width, 6);
        EXPECT_EQ(hm->threshold, 2055);
        EXPECT_EQ(hm->length, 0);
        EXPECT_EQ(hm->payload.size(), 0);
      });

  chunk1_->byte_count = 8;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 0, 0, 0};  // One byte of padding.
  defrag_->Push(std::move(chunk1_));

  auto chunk = std::make_unique<HeatmapChunk>(HeatmapChunk{
      0,                            // Vendor id.
      1,                            // Protocol version.
      3,                            // Scan time.
      8,                            // Byte count.
      std::nullopt,                 // Sequence ID.
      ReportType::kFirst,           // Report type.
      {3, 4, 5, 6, 7, 8, 0, 0, 0},  // Payload with one byte of padding.
  });
  defrag_->Push(std::move(chunk));

  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, InvalidPayloadLength) {
  chunk1_->byte_count = 8;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 0};
  defrag_->Push(std::move(chunk1_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, InvalidChunk) {
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 0};
  defrag_->Push(std::move(chunk1_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, ValidMultiChunksFirstChunkNoData) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> hm) {
        EXPECT_EQ(hm->vendor_id, 0);
        EXPECT_EQ(hm->protocol_version, 1);
        EXPECT_EQ(hm->scan_time, 2);
        EXPECT_EQ(hm->encoding, 3);
        EXPECT_EQ(hm->bit_depth, 4);
        EXPECT_EQ(hm->height, 5);
        EXPECT_EQ(hm->width, 6);
        EXPECT_EQ(hm->threshold, 2055);
        EXPECT_EQ(hm->length, 2);
        EXPECT_EQ(hm->payload.size(), 2);
        EXPECT_EQ(hm->payload[0], 21);
        EXPECT_EQ(hm->payload[1], 22);
      });

  chunk1_->byte_count = 10;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 2, 0};

  chunk2_->sequence_id = 1;
  chunk2_->report_type = ReportType::kSubsequent;
  chunk2_->payload = {21, 22, 0};  // One byte of padding.

  defrag_->Push(std::move(chunk1_));
  defrag_->Push(std::move(chunk2_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, ValidMultiChunks) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> hm) {
        EXPECT_EQ(hm->vendor_id, 0);
        EXPECT_EQ(hm->protocol_version, 1);
        EXPECT_EQ(hm->scan_time, 2);
        EXPECT_EQ(hm->encoding, 3);
        EXPECT_EQ(hm->bit_depth, 4);
        EXPECT_EQ(hm->height, 5);
        EXPECT_EQ(hm->width, 6);
        EXPECT_EQ(hm->threshold, 2055);
        EXPECT_EQ(hm->length, 4);
        EXPECT_EQ(hm->payload.size(), 4);
        EXPECT_EQ(hm->payload[0], 21);
        EXPECT_EQ(hm->payload[1], 22);
        EXPECT_EQ(hm->payload[2], 23);
        EXPECT_EQ(hm->payload[3], 24);
      });

  chunk1_->byte_count = 12;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 4, 0, 21, 22};

  chunk2_->sequence_id = 1;
  chunk2_->report_type = ReportType::kSubsequent;
  chunk2_->payload = {23, 24, 0};  // One byte of padding.

  defrag_->Push(std::move(chunk1_));
  defrag_->Push(std::move(chunk2_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, InvalidMultiChunks) {
  auto chunk = std::make_unique<HeatmapChunk>(HeatmapChunk{
      0,                             // Vendor id.
      1,                             // Protocol version.
      2,                             // Scan time.
      16,                            // Byte count.
      std::nullopt,                  // Sequence ID.
      ReportType::kFirst,            // Report type.
      {3, 4, 5, 6, 7, 8, 8, 0, 10},  // Payload.
  });

  chunk2_->sequence_id = 1;
  chunk1_->report_type = ReportType::kSubsequent;
  chunk1_->payload = {11, 12, 13, 14};

  chunk2_->sequence_id = 2;
  chunk2_->report_type = ReportType::kSubsequent;
  chunk2_->payload = {15, 16, 17, 1};  // One byte of nonzero padding.

  defrag_->Push(std::move(chunk));
  defrag_->Push(std::move(chunk1_));
  defrag_->Push(std::move(chunk2_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, IncompleteFrame) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> hm) {
        EXPECT_EQ(hm->vendor_id, 0);
        EXPECT_EQ(hm->protocol_version, 1);
        EXPECT_EQ(hm->scan_time, 2);
        EXPECT_EQ(hm->encoding, 3);
        EXPECT_EQ(hm->bit_depth, 4);
        EXPECT_EQ(hm->height, 5);
        EXPECT_EQ(hm->width, 6);
        EXPECT_EQ(hm->threshold, 2055);
        EXPECT_EQ(hm->length, 2);
        EXPECT_EQ(hm->payload.size(), 2);
      });

  auto chunk = std::make_unique<HeatmapChunk>(HeatmapChunk{
      0,                                 // Vendor id.
      1,                                 // Protocol version.
      1,                                 // Scan time.
      16,                                // Byte count.
      std::nullopt,                      // Sequence ID.
      ReportType::kFirst,                // Report type.
      {3, 4, 5, 6, 7, 8, 8, 0, 10, 11},  // Payload.
  });

  chunk1_->byte_count = 10;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 2, 0};

  chunk2_->sequence_id = 1;
  chunk2_->report_type = ReportType::kSubsequent;
  chunk2_->payload = {21, 22};  // No padding.

  defrag_->Push(std::move(chunk));
  defrag_->Push(std::move(chunk1_));
  defrag_->Push(std::move(chunk2_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, DisruptedSequences) {
  chunk1_->byte_count = 10;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 2, 0};

  chunk2_->sequence_id = 2;
  chunk2_->report_type = ReportType::kSubsequent;
  chunk2_->payload = {21, 22, 0, 0, 0, 0, 0, 0};

  defrag_->Push(std::move(chunk1_));
  defrag_->Push(std::move(chunk2_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, IncorrectLength) {
  chunk1_->byte_count = 12;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 2, 0, 21, 22, 23, 0, 0, 0};

  chunk2_->sequence_id = 1;
  chunk2_->report_type = ReportType::kSubsequent;
  chunk2_->payload = {21, 22, 0, 0, 0, 0, 0, 0};

  defrag_->Push(std::move(chunk1_));
  defrag_->Push(std::move(chunk2_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, DifferentChunkSize) {
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> hm) {
        EXPECT_EQ(hm->vendor_id, 0);
        EXPECT_EQ(hm->protocol_version, 1);
        EXPECT_EQ(hm->scan_time, 2);
        EXPECT_EQ(hm->encoding, 3);
        EXPECT_EQ(hm->bit_depth, 4);
        EXPECT_EQ(hm->height, 5);
        EXPECT_EQ(hm->width, 6);
        EXPECT_EQ(hm->threshold, 2055);
        EXPECT_EQ(hm->length, 2);
        EXPECT_EQ(hm->payload.size(), 2);
        EXPECT_EQ(hm->payload[0], 21);
        EXPECT_EQ(hm->payload[1], 0);
      });

  chunk1_->byte_count = 10;
  chunk1_->report_type = ReportType::kFirst;
  chunk1_->payload = {3, 4, 5, 6, 7, 8, 2, 0};

  chunk2_->sequence_id = 1;
  chunk2_->report_type = ReportType::kSubsequent;
  chunk2_->payload = {21, 0, 0, 0, 0,
                      0,  0, 0, 0};  // The last valid data happens to be 0.

  defrag_->Push(std::move(chunk1_));
  defrag_->Push(std::move(chunk2_));
  task_environment_.RunUntilIdle();
}

TEST_F(DefragmenterTest, CheckPayloadHeader) {
  std::vector<uint8_t> payload = {0, 1, 2, 3};
  EXPECT_FALSE(defrag_->GetPayloadHeader(payload));

  payload = {0, 1, 2, 3, 4, 5, 6, 7};
  defrag_->byte_count_ = 1806;
  EXPECT_TRUE(defrag_->GetPayloadHeader(payload));
  EXPECT_EQ(defrag_->hm_->encoding, 0);
  EXPECT_EQ(defrag_->hm_->bit_depth, 1);
  EXPECT_EQ(defrag_->hm_->height, 2);
  EXPECT_EQ(defrag_->hm_->width, 3);
  EXPECT_EQ(defrag_->hm_->threshold, (4 | (5 << 8)));
  EXPECT_EQ(defrag_->hm_->length, (6 | (7 << 8)));
}

TEST_F(DefragmenterTest, InvalidZeroPadding) {
  std::vector<uint8_t> payload = {0, 1, 2, 3, 4, 5, 2, 0, 1, 1, 1, 0, 0, 0, 0};
  defrag_->byte_count_ = 10;

  EXPECT_FALSE(defrag_->ValidatePadding(payload, 10));
}

TEST_F(DefragmenterTest, ChunkValidation) {
  EXPECT_FALSE(defrag_->ValidateChunk(nullptr));

  EXPECT_FALSE(defrag_->ValidateChunk(chunk1_.get()));

  chunk1_->report_type = ReportType::kFirst;
  EXPECT_FALSE(defrag_->ValidateChunk(chunk1_.get()));

  chunk1_->report_type = ReportType::kSubsequent;
  chunk1_->sequence_id = 1;
  EXPECT_TRUE(defrag_->ValidateChunk(chunk1_.get()));
}

}  // namespace touchraw
