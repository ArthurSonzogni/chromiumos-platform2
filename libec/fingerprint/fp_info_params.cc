// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_params.h"

#include <tuple>

#include "libec/ec_command.h"

bool operator==(const struct fp_image_frame_params& lhs,
                const struct fp_image_frame_params& rhs) {
  return std::tie(lhs.bpp, lhs.frame_size, lhs.pixel_format, lhs.width,
                  lhs.height, lhs.fp_capture_type, lhs.reserved) ==
         std::tie(rhs.bpp, rhs.frame_size, rhs.pixel_format, rhs.width,
                  rhs.height, rhs.fp_capture_type, rhs.reserved);
}
