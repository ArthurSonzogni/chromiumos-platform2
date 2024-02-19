// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/heatmap_consumer.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/test/simple_test_clock.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libtouchraw/touchraw.h>

#include "ml/heatmap_processor.h"

namespace ml {
namespace {
using ::testing::_;

class MockHeatmapProcessor : public HeatmapProcessor {
 public:
  MockHeatmapProcessor() = default;

  MOCK_METHOD(void,
              Process,
              (const std::vector<double>& heatmap_data,
               int height,
               int width,
               base::Time timestamp),
              (override));
};
}  //  namespace

TEST(HeatmapConsumerTest, PushesData) {
  testing::StrictMock<MockHeatmapProcessor> mock_processor;
  base::SimpleTestClock clock;
  HeatmapConsumer consumer(&mock_processor, &clock);
  auto heatmap = std::make_unique<touchraw::Heatmap>();
  heatmap->height = 1;
  heatmap->width = 2;
  heatmap->payload = std::vector<uint8_t>({0x01, 0x23, 0x45, 0x67});

  clock.Advance(base::Minutes(12345));

  EXPECT_CALL(mock_processor,
              Process(std::vector<double>({static_cast<double>(0x2301),
                                           static_cast<double>(0x6745)}),
                      /*height=*/1,
                      /*width=*/2, clock.Now()))
      .Times(1);

  consumer.Push(std::move(heatmap));
}

}  //  namespace ml
