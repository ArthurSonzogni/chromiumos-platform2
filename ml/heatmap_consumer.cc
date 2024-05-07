// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/heatmap_consumer.h"

#include <vector>

namespace ml {
namespace {
constexpr int kHeatmapLowerThreshold = 75;
constexpr int kHeatmapHigherThreshold = 32768;
}  // namespace

HeatmapConsumer::HeatmapConsumer(HeatmapProcessor* processor,
                                 base::Clock* clock)
    : processor_(processor), clock_(clock) {}

void HeatmapConsumer::Push(std::unique_ptr<touchraw::Heatmap> heatmap) {
  int height = heatmap->height;
  int width = heatmap->width;
  std::vector<double> data;
  // The payload in `heatmap` is an array of bytes, each consecutive two bytes
  // represent the value of one heatmap pixel, with little-endian encoding.
  // Here, we first decode the values and then cast the values to double so that
  // they can be inputted to the neural network model.
  for (int i = 0; i < height * width; i++) {
    int x = static_cast<int>(heatmap->payload[i * 2]) +
            (static_cast<int>(heatmap->payload[i * 2 + 1]) << 8);
    if (x <= kHeatmapLowerThreshold || x >= kHeatmapHigherThreshold) {
      x = 0;
    }
    data.push_back(static_cast<double>(x));
  }
  if (processor_) {
    processor_->Process(data, height, width, clock_->Now());
  }
}
}  // namespace ml
