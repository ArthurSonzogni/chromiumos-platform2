// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_HEATMAP_CONSUMER_H_
#define ML_HEATMAP_CONSUMER_H_

#include <memory>

#include <base/time/default_clock.h>
#include <libtouchraw/consumer_interface.h>
#include <libtouchraw/touchraw.h>

#include "ml/heatmap_processor.h"

namespace ml {
// An implementation of `touchraw::HeatmapConsumerInterface` to be used in ML
// service. It is responsible for parsing heatmap data from libtouchraw and
// and sending the parsed data to further processing, e.g. NN inference.
class HeatmapConsumer : public touchraw::HeatmapConsumerInterface {
 public:
  // Construct the consumer object with a processor pointer and an optional
  // clock pointer. Neither pointer is owned by the consumer object.
  explicit HeatmapConsumer(
      HeatmapProcessor* processor,
      base::Clock* clock = base::DefaultClock::GetInstance());

  // touchraw::HeatmapConsumerInterface:
  void Push(std::unique_ptr<touchraw::Heatmap> heatmap) override;

 private:
  HeatmapProcessor* processor_;
  base::Clock* const clock_;
};
}  // namespace ml

#endif  //  ML_HEATMAP_CONSUMER_H_
