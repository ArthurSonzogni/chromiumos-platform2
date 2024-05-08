// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_RESHAPER_H_
#define LIBTOUCHRAW_RESHAPER_H_

#include <cstdint>
#include <memory>
#include <utility>

#include <base/task/sequenced_task_runner.h>
#include <gtest/gtest_prod.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/crop.h"
#include "libtouchraw/touchraw.h"
#include "libtouchraw/touchraw_export.h"

namespace touchraw {

class LIBTOUCHRAW_EXPORT Reshaper : public HeatmapConsumerInterface {
 public:
  /**
   * Reshaper takes a heatmap and crops it according to the crop
   * specification. Reshaping is sometimes needed when feeding the heatmap into
   * a consumer that is expecting a fixed size heatmap. This initial
   * implementation only supports cropping.
   *
   * @param crop Crop specification to be applied.
   * @param queue Heatmap consumer queue for tasks to be posted.
   */
  explicit Reshaper(Crop crop, std::unique_ptr<HeatmapConsumerInterface> queue);

  Reshaper(const Reshaper&) = delete;
  Reshaper& operator=(const Reshaper&) = delete;

  void Push(std::unique_ptr<Heatmap> heatmap) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Reshaper::ReshapeHeatmap,
                                  base::Unretained(this), std::move(heatmap)));
  }

 protected:
  /**
   * Reshape parsed heatmap events.
   *
   * @param heatmap The parsed heatmap that will be reshaped.
   */
  void ReshapeHeatmap(std::unique_ptr<Heatmap> heatmap);

 private:
  // The crop specification to be applied to the heatmap.
  Crop crop_;
  // Task queue.
  const std::unique_ptr<HeatmapConsumerInterface> queue_;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_RESHAPER_H_
