// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtouchraw/reshaper.h"

#include <climits>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <base/logging.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/crop.h"
#include "libtouchraw/touchraw.h"
#include "libtouchraw/touchraw_export.h"

namespace touchraw {

Reshaper::Reshaper(Crop crop, std::unique_ptr<HeatmapConsumerInterface> queue)
    : crop_(crop), queue_(std::move(queue)) {}

void Reshaper::ReshapeHeatmap(std::unique_ptr<Heatmap> heatmap) {
  if (((crop_.top_crop + crop_.bottom_crop) > heatmap->height) ||
      ((crop_.left_crop + crop_.right_crop) > heatmap->width)) {
    LOG(WARNING) << "Skipping attempt to crop beyond heatmap bounds.";
    queue_->Push(std::move(heatmap));
    return;
  }

  unsigned int bytes_per_cell =
      std::ceil(heatmap->bit_depth / (CHAR_BIT * 1.0));

  const uint8_t row_end = heatmap->height - crop_.bottom_crop;
  const uint8_t column_end = heatmap->width - crop_.right_crop;
  std::vector<uint8_t>::size_type destination = 0;
  std::vector<uint8_t>::size_type source = 0;
  for (uint8_t r = crop_.top_crop; r < row_end; ++r) {
    for (uint8_t c = crop_.left_crop; c < column_end; ++c) {
      source = ((r * heatmap->width) + c) * bytes_per_cell;
      for (uint8_t i = 0; i < bytes_per_cell; ++i) {
        heatmap->payload.at(destination++) = heatmap->payload.at(source + i);
      }
    }
  }

  heatmap->length = destination;
  heatmap->payload.resize(heatmap->length);
  heatmap->height -= (crop_.top_crop + crop_.bottom_crop);
  heatmap->width -= (crop_.left_crop + crop_.right_crop);
  queue_->Push(std::move(heatmap));
}

}  // namespace touchraw
