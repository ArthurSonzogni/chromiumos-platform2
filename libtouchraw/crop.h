// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_CROP_H_
#define LIBTOUCHRAW_CROP_H_

#include <cstdint>

#include "libtouchraw/touchraw_export.h"

namespace touchraw {

/**
 * Basic cropping specification that allows for cropping by a number of rows
 * and a number of columns from all directions. This basic specification means
 * that not all possible croppings are possible but it is sufficient for
 * cropping heatmaps of touchscreens into other touchscreen sizes.
 *
 */
struct LIBTOUCHRAW_EXPORT Crop {
  // Number of rows of data to crop from the bottom.
  uint8_t bottom_crop;
  // Number of columns of data to crop from the left.
  uint8_t left_crop;
  // Number of columns of data to crop from the right.
  uint8_t right_crop;
  // Number of rows of data to crop from the top.
  uint8_t top_crop;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_CROP_H_
