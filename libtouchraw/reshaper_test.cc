// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "libtouchraw/crop.h"
#include "libtouchraw/mock_consumers.h"
#include "libtouchraw/reshaper.h"

namespace touchraw {

namespace test {
struct ReshaperTestParam {
  uint8_t crop_bottom;
  uint8_t crop_left;
  uint8_t crop_right;
  uint8_t crop_top;
  unsigned int expected_heatmap_size;
  std::string name;
};
}  // namespace test

class ReshaperTest
    : public testing::TestWithParam<touchraw::test::ReshaperTestParam> {
 protected:
  void validatePayload(const touchraw::test::ReshaperTestParam& param,
                       const std::unique_ptr<const Heatmap>& heatmap,
                       const std::vector<uint8_t>& payload,
                       const unsigned int num_bytes) {
    std::vector<uint8_t>::size_type heatmap_index = 0;
    std::vector<uint8_t>::size_type expected_value_index = 0;

    for (std::vector<uint8_t>::size_type r = param.crop_top;
         r < (height - param.crop_bottom); ++r) {
      for (std::vector<uint8_t>::size_type c = param.crop_left;
           c < (width - param.crop_right); ++c) {
        expected_value_index = ((r * width) + c) * num_bytes;
        for (unsigned int i = 0; i < num_bytes; ++i) {
          EXPECT_EQ(heatmap->payload.at(heatmap_index++),
                    payload.at(expected_value_index + i));
        }
      }
    }
  }

  std::vector<uint8_t> payload_8bit_6_by_5 = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
      0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
      0x15, 0x16, 0x17, 0x18, 0x19, 0X1A, 0x1B, 0x1C, 0x1D, 0x1E,
  };
  std::vector<uint8_t> payload_12bit_6_by_5 = {
      0x01, 0x0F, 0x02, 0x0E, 0x03, 0x0D, 0x04, 0x08, 0x05, 0x0A, 0x06, 0x0F,
      0x07, 0x0F, 0x08, 0x0E, 0x09, 0x0D, 0x0A, 0x08, 0x0B, 0x0A, 0x0C, 0x0F,
      0x0D, 0x0F, 0x0E, 0x0E, 0x0F, 0x0D, 0x10, 0x08, 0x11, 0x0A, 0x12, 0x0F,
      0x13, 0x0F, 0x14, 0x0E, 0x15, 0x0D, 0x16, 0x08, 0x17, 0x0A, 0x18, 0x0F,
      0x19, 0x0F, 0X1A, 0x0E, 0x1B, 0x0D, 0x1C, 0x08, 0x1D, 0x0A, 0x1E, 0x0F,
  };
  std::vector<uint8_t> payload_16bit_6_by_5 = {
      0x01, 0x4F, 0x02, 0x5E, 0x03, 0x7D, 0x04, 0x18, 0x05, 0xAA, 0x06, 0xCF,
      0x07, 0x4F, 0x08, 0x5E, 0x09, 0x7D, 0x0A, 0x18, 0x0B, 0xAA, 0x0C, 0xCF,
      0x0D, 0x4F, 0x0E, 0x5E, 0x0F, 0x7D, 0x10, 0x18, 0x11, 0xAA, 0x12, 0xCF,
      0x13, 0x4F, 0x14, 0x5E, 0x15, 0x7D, 0x16, 0x18, 0x17, 0xAA, 0x18, 0xCF,
      0x19, 0x4F, 0X1A, 0x5E, 0x1B, 0x7D, 0x1C, 0x18, 0x1D, 0xAA, 0x1E, 0xCF,
  };
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  const uint8_t height = 5;
  const uint8_t width = 6;
};

class ReshaperBeyondBoundsTest : public ReshaperTest {
 protected:
  void validatePayload(const std::unique_ptr<const Heatmap>& heatmap,
                       const std::vector<uint8_t>& payload,
                       const unsigned int num_bytes) {
    std::vector<uint8_t>::size_type heatmap_index = 0;
    std::vector<uint8_t>::size_type expected_value_index = 0;

    for (std::vector<uint8_t>::size_type r = 0; r < height; ++r) {
      for (std::vector<uint8_t>::size_type c = 0; c < width; ++c) {
        expected_value_index = ((r * width) + c) * num_bytes;
        for (unsigned int i = 0; i < num_bytes; ++i) {
          EXPECT_EQ(heatmap->payload.at(heatmap_index++),
                    payload.at(expected_value_index + i));
        }
      }
    }
  }
};

TEST_P(ReshaperTest, Crop8BitHeatmap) {
  std::unique_ptr<Heatmap> heatmap_8bit = std::make_unique<Heatmap>(Heatmap{
      0,                    // Vendor id.
      1,                    // Protocol version.
      2,                    // Scan time.
      kDiffData,            // Encoding.
      8,                    // Bit Depth.
      height,               // Height.
      width,                // Width.
      10,                   // Threshold.
      30,                   // Payload length in bytes.
      payload_8bit_6_by_5,  // Payload.
  });
  touchraw::test::ReshaperTestParam param = GetParam();

  auto mock_consumer =
      std::make_unique<testing::StrictMock<MockHeatmapConsumer>>();
  auto mock_consumer_ptr = mock_consumer.get();
  EXPECT_CALL(*mock_consumer_ptr, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> heatmap) {
        EXPECT_EQ(heatmap->vendor_id, 0);
        EXPECT_EQ(heatmap->protocol_version, 1);
        EXPECT_EQ(heatmap->scan_time, 2);
        EXPECT_EQ(heatmap->encoding, 1);
        EXPECT_EQ(heatmap->bit_depth, 8);
        EXPECT_EQ(heatmap->threshold, 10);
        EXPECT_EQ(heatmap->height,
                  (height - param.crop_top - param.crop_bottom));
        EXPECT_EQ(heatmap->width, (width - param.crop_left - param.crop_right));
        EXPECT_EQ(heatmap->length, param.expected_heatmap_size);
        EXPECT_EQ(heatmap->payload.size(), param.expected_heatmap_size);

        // Validate payload.
        validatePayload(param, heatmap, payload_8bit_6_by_5, /*num_bytes=*/1);
      });
  auto reshaper = std::make_unique<Reshaper>(
      Crop{
          param.crop_bottom,
          param.crop_left,
          param.crop_right,
          param.crop_top,
      },
      std::move(mock_consumer));
  reshaper->Push(std::move(heatmap_8bit));
  task_environment_.RunUntilIdle();
}

TEST_P(ReshaperTest, Crop12BitHeatmap) {
  std::unique_ptr<Heatmap> heatmap_12bit = std::make_unique<Heatmap>(Heatmap{
      0,                     // Vendor id.
      1,                     // Protocol version.
      3,                     // Scan time.
      kDiffData,             // Encoding.
      12,                    // Bit Depth.
      height,                // Height.
      width,                 // Width.
      10,                    // Threshold.
      60,                    // Payload length in bytes.
      payload_12bit_6_by_5,  // Payload.
  });
  touchraw::test::ReshaperTestParam param = GetParam();

  auto mock_consumer =
      std::make_unique<testing::StrictMock<MockHeatmapConsumer>>();
  auto mock_consumer_ptr = mock_consumer.get();
  EXPECT_CALL(*mock_consumer_ptr, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> heatmap) {
        EXPECT_EQ(heatmap->vendor_id, 0);
        EXPECT_EQ(heatmap->protocol_version, 1);
        EXPECT_EQ(heatmap->scan_time, 3);
        EXPECT_EQ(heatmap->encoding, 1);
        EXPECT_EQ(heatmap->bit_depth, 12);
        EXPECT_EQ(heatmap->threshold, 10);
        EXPECT_EQ(heatmap->height,
                  (height - param.crop_top - param.crop_bottom));
        EXPECT_EQ(heatmap->width, (width - param.crop_left - param.crop_right));
        EXPECT_EQ(heatmap->length, 2 * param.expected_heatmap_size);
        EXPECT_EQ(heatmap->payload.size(), 2 * param.expected_heatmap_size);

        // Validate payload.
        validatePayload(param, heatmap, payload_12bit_6_by_5, /*num_bytes=*/2);
      });
  auto reshaper = std::make_unique<Reshaper>(
      Crop{
          param.crop_bottom,
          param.crop_left,
          param.crop_right,
          param.crop_top,
      },
      std::move(mock_consumer));
  reshaper->Push(std::move(heatmap_12bit));
  task_environment_.RunUntilIdle();
}

TEST_P(ReshaperTest, Crop16BitHeatmap) {
  std::unique_ptr<Heatmap> heatmap_16bit = std::make_unique<Heatmap>(Heatmap{
      0,                     // Vendor id.
      1,                     // Protocol version.
      3,                     // Scan time.
      kDiffData,             // Encoding.
      16,                    // Bit Depth.
      height,                // Height.
      width,                 // Width.
      10,                    // Threshold.
      60,                    // Payload length in bytes.
      payload_16bit_6_by_5,  // Payload.
  });
  touchraw::test::ReshaperTestParam param = GetParam();

  auto mock_consumer =
      std::make_unique<testing::StrictMock<MockHeatmapConsumer>>();
  auto mock_consumer_ptr = mock_consumer.get();
  EXPECT_CALL(*mock_consumer_ptr, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> heatmap) {
        EXPECT_EQ(heatmap->vendor_id, 0);
        EXPECT_EQ(heatmap->protocol_version, 1);
        EXPECT_EQ(heatmap->scan_time, 3);
        EXPECT_EQ(heatmap->encoding, 1);
        EXPECT_EQ(heatmap->bit_depth, 16);
        EXPECT_EQ(heatmap->threshold, 10);
        EXPECT_EQ(heatmap->height,
                  (height - param.crop_top - param.crop_bottom));
        EXPECT_EQ(heatmap->width, (width - param.crop_left - param.crop_right));
        EXPECT_EQ(heatmap->length, 2 * param.expected_heatmap_size);
        EXPECT_EQ(heatmap->payload.size(), 2 * param.expected_heatmap_size);

        // Validate payload.
        validatePayload(param, heatmap, payload_16bit_6_by_5, /*num_bytes=*/2);
      });
  auto reshaper = std::make_unique<Reshaper>(
      Crop{
          param.crop_bottom,
          param.crop_left,
          param.crop_right,
          param.crop_top,
      },
      std::move(mock_consumer));
  reshaper->Push(std::move(heatmap_16bit));
  task_environment_.RunUntilIdle();
}

TEST_P(ReshaperBeyondBoundsTest, CropBeyondBounds) {
  std::unique_ptr<Heatmap> heatmap_8bit = std::make_unique<Heatmap>(Heatmap{
      0,                    // Vendor id.
      1,                    // Protocol version.
      2,                    // Scan time.
      kDiffData,            // Encoding.
      8,                    // Bit Depth.
      height,               // Height.
      width,                // Width.
      10,                   // Threshold.
      30,                   // Payload length in bytes.
      payload_8bit_6_by_5,  // Payload.
  });
  touchraw::test::ReshaperTestParam param = GetParam();

  auto mock_consumer =
      std::make_unique<testing::StrictMock<MockHeatmapConsumer>>();
  auto mock_consumer_ptr = mock_consumer.get();
  EXPECT_CALL(*mock_consumer_ptr, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> heatmap) {
        EXPECT_EQ(heatmap->vendor_id, 0);
        EXPECT_EQ(heatmap->protocol_version, 1);
        EXPECT_EQ(heatmap->scan_time, 2);
        EXPECT_EQ(heatmap->encoding, 1);
        EXPECT_EQ(heatmap->bit_depth, 8);
        EXPECT_EQ(heatmap->threshold, 10);
        EXPECT_EQ(heatmap->height, height);
        EXPECT_EQ(heatmap->width, width);
        EXPECT_EQ(heatmap->length, param.expected_heatmap_size);
        EXPECT_EQ(heatmap->payload.size(), param.expected_heatmap_size);

        // Validate payload.
        validatePayload(heatmap, payload_8bit_6_by_5, /*num_bytes=*/1);
      });
  auto reshaper = std::make_unique<Reshaper>(
      Crop{
          param.crop_bottom,
          param.crop_left,
          param.crop_right,
          param.crop_top,
      },
      std::move(mock_consumer));
  reshaper->Push(std::move(heatmap_8bit));
  task_environment_.RunUntilIdle();
}

TEST_P(ReshaperBeyondBoundsTest, Crop12BitBeyondBounds) {
  std::unique_ptr<Heatmap> heatmap_12bit = std::make_unique<Heatmap>(Heatmap{
      0,                     // Vendor id.
      1,                     // Protocol version.
      4,                     // Scan time.
      kDiffData,             // Encoding.
      12,                    // Bit Depth.
      height,                // Height.
      width,                 // Width.
      10,                    // Threshold.
      60,                    // Payload length in bytes.
      payload_12bit_6_by_5,  // Payload.
  });
  touchraw::test::ReshaperTestParam param = GetParam();

  auto mock_consumer =
      std::make_unique<testing::StrictMock<MockHeatmapConsumer>>();
  auto mock_consumer_ptr = mock_consumer.get();
  EXPECT_CALL(*mock_consumer_ptr, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> heatmap) {
        EXPECT_EQ(heatmap->vendor_id, 0);
        EXPECT_EQ(heatmap->protocol_version, 1);
        EXPECT_EQ(heatmap->scan_time, 4);
        EXPECT_EQ(heatmap->encoding, 1);
        EXPECT_EQ(heatmap->bit_depth, 12);
        EXPECT_EQ(heatmap->threshold, 10);
        EXPECT_EQ(heatmap->height, height);
        EXPECT_EQ(heatmap->width, width);
        EXPECT_EQ(heatmap->length, 2 * param.expected_heatmap_size);
        EXPECT_EQ(heatmap->payload.size(), 2 * param.expected_heatmap_size);

        // Validate payload.
        validatePayload(heatmap, payload_12bit_6_by_5, /*num_bytes=*/2);
      });
  auto reshaper = std::make_unique<Reshaper>(
      Crop{
          param.crop_bottom,
          param.crop_left,
          param.crop_right,
          param.crop_top,
      },
      std::move(mock_consumer));
  reshaper->Push(std::move(heatmap_12bit));
  task_environment_.RunUntilIdle();
}

TEST_P(ReshaperBeyondBoundsTest, Crop16BitBeyondBounds) {
  std::unique_ptr<Heatmap> heatmap_16bit = std::make_unique<Heatmap>(Heatmap{
      0,                     // Vendor id.
      1,                     // Protocol version.
      4,                     // Scan time.
      kDiffData,             // Encoding.
      16,                    // Bit Depth.
      height,                // Height.
      width,                 // Width.
      10,                    // Threshold.
      60,                    // Payload length in bytes.
      payload_16bit_6_by_5,  // Payload.
  });
  touchraw::test::ReshaperTestParam param = GetParam();

  auto mock_consumer =
      std::make_unique<testing::StrictMock<MockHeatmapConsumer>>();
  auto mock_consumer_ptr = mock_consumer.get();
  EXPECT_CALL(*mock_consumer_ptr, Push(testing::_))
      .WillOnce([&](std::unique_ptr<const Heatmap> heatmap) {
        EXPECT_EQ(heatmap->vendor_id, 0);
        EXPECT_EQ(heatmap->protocol_version, 1);
        EXPECT_EQ(heatmap->scan_time, 4);
        EXPECT_EQ(heatmap->encoding, 1);
        EXPECT_EQ(heatmap->bit_depth, 16);
        EXPECT_EQ(heatmap->threshold, 10);
        EXPECT_EQ(heatmap->height, height);
        EXPECT_EQ(heatmap->width, width);
        EXPECT_EQ(heatmap->length, 2 * param.expected_heatmap_size);
        EXPECT_EQ(heatmap->payload.size(), 2 * param.expected_heatmap_size);

        // Validate payload.
        validatePayload(heatmap, payload_16bit_6_by_5, /*num_bytes=*/2);
      });
  auto reshaper = std::make_unique<Reshaper>(
      Crop{
          param.crop_bottom,
          param.crop_left,
          param.crop_right,
          param.crop_top,
      },
      std::move(mock_consumer));
  reshaper->Push(std::move(heatmap_16bit));
  task_environment_.RunUntilIdle();
}

INSTANTIATE_TEST_SUITE_P(
    ReshaperSuite,
    ReshaperTest,
    testing::Values(
        touchraw::test::ReshaperTestParam{1, 1, 1, 1, 12,
                                          "CropAllSidesEqually"},
        touchraw::test::ReshaperTestParam{0, 0, 0, 2, 18, "CropTopBy2"},
        touchraw::test::ReshaperTestParam{0, 2, 0, 0, 20, "CropLeftBy2"},
        touchraw::test::ReshaperTestParam{1, 0, 0, 0, 24, "CropBottomBy1"},
        touchraw::test::ReshaperTestParam{0, 0, 1, 0, 25, "CropRightBy1"},
        touchraw::test::ReshaperTestParam{0, 5, 0, 4, 1,
                                          "CropTillBottomRightCell"},
        touchraw::test::ReshaperTestParam{0, 0, 6, 0, 0, "CropAllFromRight"},
        touchraw::test::ReshaperTestParam{5, 0, 0, 0, 0, "CropAllFromBottom"},
        touchraw::test::ReshaperTestParam{5, 0, 6, 0, 0, "CropAllBottomRight"}),
    [](const testing::TestParamInfo<ReshaperTest::ParamType>& info) {
      return info.param.name;
    });

INSTANTIATE_TEST_SUITE_P(
    ReshaperSuite,
    ReshaperBeyondBoundsTest,
    testing::Values(
        touchraw::test::ReshaperTestParam{6, 0, 0, 0, 30,
                                          "CropBeyondBoundsFromBottom"},
        touchraw::test::ReshaperTestParam{0, 7, 0, 0, 30,
                                          "CropBeyondBoundsFromLeft"}),
    [](const testing::TestParamInfo<ReshaperTest::ParamType>& info) {
      return info.param.name;
    });

}  // namespace touchraw
