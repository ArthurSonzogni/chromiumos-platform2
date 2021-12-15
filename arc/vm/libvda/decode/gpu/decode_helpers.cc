// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/libvda/decode/gpu/decode_helpers.h"

namespace arc {

arc::mojom::HalPixelFormat ConvertPixelFormatToHalPixelFormat(
    vda_pixel_format_t format) {
  switch (format) {
    case YV12:
      return arc::mojom::HalPixelFormat::HAL_PIXEL_FORMAT_YV12;
    case NV12:
      return arc::mojom::HalPixelFormat::HAL_PIXEL_FORMAT_NV12;
    default:
      NOTREACHED();
  }
}

bool CheckValidOutputFormat(vda_pixel_format_t format, size_t num_planes) {
  switch (format) {
    case NV12:
      if (num_planes != 2) {
        LOG(ERROR) << "Invalid number of planes for NV12 format, expected 2 "
                      "but received "
                   << num_planes;
        return false;
      }
      break;
    case YV12:
      if (num_planes != 3) {
        LOG(ERROR) << "Invalid number of planes for YV12 format, expected 3 "
                      "but received "
                   << num_planes;
        return false;
      }
      break;
    default:
      LOG(WARNING) << "Unexpected format: " << format;
      return false;
  }
  return true;
}

}  // namespace arc
