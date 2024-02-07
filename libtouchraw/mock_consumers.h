// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_MOCK_CONSUMERS_H_
#define LIBTOUCHRAW_MOCK_CONSUMERS_H_

#include <memory>

#include <gmock/gmock.h>

#include "libtouchraw/consumer_interface.h"

namespace touchraw {

class MockHeatmapConsumer : public HeatmapConsumerInterface {
 public:
  MOCK_METHOD(void, Push, (std::unique_ptr<const Heatmap> data), (override));
};

class MockHeatmapChunkConsumer : public HeatmapChunkConsumerInterface {
 public:
  MOCK_METHOD(void,
              Push,
              (std::unique_ptr<const HeatmapChunk> data),
              (override));
};

class MockHIDDataConsumer : public HIDDataConsumerInterface {
 public:
  MOCK_METHOD(void, Push, (std::unique_ptr<const HIDData> data), (override));
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_MOCK_CONSUMERS_H_
