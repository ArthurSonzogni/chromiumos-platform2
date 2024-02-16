// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtouchraw/parser.h"

#include <string>

#include <base/files/file_util.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/mock_consumers.h"

namespace touchraw {

// TODO: b/275615279 - Add unit tests for ReportDescriptor class.

base::FilePath GetTestDataPath(const std::string& name) {
  base::FilePath src = base::FilePath(getenv("SRC"));
  src = src.Append("test_data");
  return src.Append(name);
}

class ParserTest : public testing::Test {
 protected:
  void SetUp() override {
    auto mock_consumer =
        std::make_unique<testing::StrictMock<MockHeatmapChunkConsumer>>();
    mock_consumer_ = mock_consumer.get();
    parser_ = Parser::CreateForTesting(std::move(mock_consumer));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  MockHeatmapChunkConsumer* mock_consumer_;
  std::unique_ptr<Parser> parser_;
};

// Complete report descriptor that supports heat map.
TEST_F(ParserTest, ValidReportDescriptorWithHeatmap) {
  hidraw_report_descriptor rpt_desc;
  auto buf = base::ReadFileToBytes(GetTestDataPath("report_descriptor.bin"));
  ASSERT_TRUE(buf.has_value());
  rpt_desc.size = buf->size();
  memcpy(rpt_desc.value, buf->data(), buf->size());

  EXPECT_TRUE(parser_->ParseHeatmapReportsFromDescriptor(&rpt_desc));
  EXPECT_EQ(parser_->usages_.size(), 82);
  EXPECT_TRUE(parser_->sync_report_offset_.has_value());
  EXPECT_EQ(parser_->usages_[parser_->sync_report_offset_.value()].report_id,
            144);
  EXPECT_TRUE(parser_->sub_report_offset_.has_value());
  EXPECT_EQ(parser_->usages_[parser_->sub_report_offset_.value()].report_id,
            145);

  // A chunk that has unmatched payload size 5, where the expected size is 7304
  // retrieved from the report descriptor.
  auto hid_data = std::make_unique<HIDData>(
      HIDData{144,  // Report id.
              {
                  0x11, 0x22,                    // Protocol vendor ID.
                  0x33, 0x44,                    // Protocol version.
                  0x55, 0x66, 0x77, 0x88,        // Scan time.
                  0x99, 0xaa,                    // Sequence ID.
                  0xbb, 0xcc, 0xdd, 0xee, 0xff,  // Frame data.
              }});
  parser_->ParseHIDData(std::move(hid_data));

  // Use heat map report descriptor information to parse a valid first chunk.
  parser_->usages_[parser_->sync_report_offset_.value() + 4].data_size = 3;
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const HeatmapChunk> chunk) {
        EXPECT_EQ(chunk->vendor_id, 0x2211);
        EXPECT_EQ(chunk->protocol_version, 0x4433);
        EXPECT_EQ(chunk->scan_time, 0x88776655);
        EXPECT_EQ(chunk->byte_count, 0xccbbaa99);
        EXPECT_FALSE(chunk->sequence_id.has_value());
        EXPECT_EQ(chunk->report_type, ReportType::kFirst);
        EXPECT_EQ(chunk->payload.size(), 3);
        EXPECT_EQ(chunk->payload[0], 0xdd);
        EXPECT_EQ(chunk->payload[1], 0xee);
        EXPECT_EQ(chunk->payload[2], 0xff);
      });
  hid_data = std::make_unique<HIDData>(
      HIDData{144,  // Report id.
              {
                  0x11, 0x22,              // Protocol vendor ID.
                  0x33, 0x44,              // Protocol version.
                  0x55, 0x66, 0x77, 0x88,  // Scan time.
                  0x99, 0xaa, 0xbb, 0xcc,  // Byte count.
                  0xdd, 0xee, 0xff,        // Frame data.
              }});
  parser_->ParseHIDData(std::move(hid_data));

  // Use heat map report descriptor information to parse a valid subsequent
  // chunk.
  parser_->usages_[parser_->sub_report_offset_.value() + 4].data_size = 5;
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const HeatmapChunk> chunk) {
        EXPECT_EQ(chunk->vendor_id, 0x2211);
        EXPECT_EQ(chunk->protocol_version, 0x4433);
        EXPECT_EQ(chunk->scan_time, 0x88776655);
        EXPECT_FALSE(chunk->byte_count.has_value());
        EXPECT_EQ(chunk->sequence_id, 0xaa99);
        EXPECT_EQ(chunk->report_type, ReportType::kSubsequent);
        EXPECT_EQ(chunk->payload.size(), 5);
        EXPECT_EQ(chunk->payload[0], 0xbb);
        EXPECT_EQ(chunk->payload[1], 0xcc);
        EXPECT_EQ(chunk->payload[2], 0xdd);
      });
  hid_data = std::make_unique<HIDData>(
      HIDData{145,  // Report id.
              {
                  0x11, 0x22,                    // Protocol vendor ID.
                  0x33, 0x44,                    // Protocol version.
                  0x55, 0x66, 0x77, 0x88,        // Scan time.
                  0x99, 0xaa,                    // Sequence ID.
                  0xbb, 0xcc, 0xdd, 0xee, 0xff,  // Frame data.
              }});
  parser_->ParseHIDData(std::move(hid_data));
}

// Complete report descriptor that does not support heat map.
TEST_F(ParserTest, ValidReportDescriptorWithoutHeatmap) {
  hidraw_report_descriptor rpt_desc;
  auto buf = base::ReadFileToBytes(
      GetTestDataPath("report_descriptor_no_heatmap.bin"));
  ASSERT_TRUE(buf.has_value());
  rpt_desc.size = buf->size();
  memcpy(rpt_desc.value, buf->data(), buf->size());

  EXPECT_FALSE(parser_->ParseHeatmapReportsFromDescriptor(&rpt_desc));
  EXPECT_EQ(parser_->usages_.size(), 72);
  EXPECT_FALSE(parser_->sync_report_offset_.has_value());
  EXPECT_FALSE(parser_->sub_report_offset_.has_value());
}

// This report descriptor contains heat map related usages with unknown hid type
// for scan time (report id 144).
TEST_F(ParserTest, UnknownHidType) {
  hidraw_report_descriptor rpt_desc;
  auto buf = base::ReadFileToBytes(GetTestDataPath("unknown_hid_type.bin"));
  ASSERT_TRUE(buf.has_value());
  rpt_desc.size = buf->size();
  memcpy(rpt_desc.value, buf->data(), buf->size());

  EXPECT_TRUE(parser_->ParseHeatmapReportsFromDescriptor(&rpt_desc));
  EXPECT_EQ(parser_->usages_.size(), 9);
  EXPECT_TRUE(parser_->sync_report_offset_.has_value());
  EXPECT_EQ(parser_->usages_[parser_->sync_report_offset_.value()].report_id,
            144);
  EXPECT_TRUE(parser_->sub_report_offset_.has_value());
  EXPECT_EQ(parser_->usages_[parser_->sub_report_offset_.value()].report_id,
            145);

  // Unsupported report ID.
  auto hid_data = std::make_unique<HIDData>(
      HIDData{140,  // Report id.
              {
                  0x11, 0x22,              // Protocol vendor ID.
                  0x33, 0x44,              // Protocol version.
                  0x55, 0x66, 0x77, 0x88,  // Scan time.
                  0x99, 0xaa, 0xbb, 0xcc,  // Byte count.
                  0xdd, 0xee, 0xff,        // Frame data.
              }});
  parser_->ParseHIDData(std::move(hid_data));

  // Report descriptor and data do not match.
  parser_->usages_[parser_->sync_report_offset_.value() + 3].data_size = 7;
  EXPECT_CALL(*mock_consumer_, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const HeatmapChunk> chunk) {
        EXPECT_EQ(chunk->vendor_id, 0x2211);
        EXPECT_EQ(chunk->protocol_version, 0x4433);
        EXPECT_EQ(chunk->scan_time, 0);
        EXPECT_EQ(chunk->byte_count, 0x88776655);
        EXPECT_FALSE(chunk->sequence_id.has_value());
        EXPECT_EQ(chunk->report_type, ReportType::kFirst);
        EXPECT_EQ(chunk->payload.size(), 7);
        EXPECT_EQ(chunk->payload[0], 0x99);
        EXPECT_EQ(chunk->payload[6], 0xff);
      });
  hid_data = std::make_unique<HIDData>(
      HIDData{144,  // Report id.
              {
                  0x11, 0x22,              // Protocol vendor ID.
                  0x33, 0x44,              // Protocol version.
                  0x55, 0x66, 0x77, 0x88,  // Scan time.
                  0x99, 0xaa, 0xbb, 0xcc,  // Byte count.
                  0xdd, 0xee, 0xff,        // Frame data.
              }});
  parser_->ParseHIDData(std::move(hid_data));
}

}  // namespace touchraw
