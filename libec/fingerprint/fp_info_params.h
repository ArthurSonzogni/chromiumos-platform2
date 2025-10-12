// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_INFO_PARAMS_H_
#define LIBEC_FINGERPRINT_FP_INFO_PARAMS_H_

#include <array>

#include "libec/ec_command.h"

namespace ec::fp_info {

// We cannot use "ec_response_fp_info_v2" directly in the FpInfoCommand class
// because the "image_frame_params" member is a variable length array.
// "Header_v2" includes everything from that struct except "image_frame_params".
// A test validates that the size of the two structs is the same to avoid any
// divergence.

struct Header_v2 {
  /* Sensor identification */
  struct fp_sensor_info sensor_info{};
  /* Template/finger current information */
  struct fp_template_info template_info{};
};

// Allocates space for the flash bank response.
struct Params_v2 {
  struct Header_v2 info{};
  ArrayData<struct fp_image_frame_params, struct Header_v2>
      image_frame_params{};
};

}  // namespace ec::fp_info

bool operator==(const struct fp_image_frame_params& lhs,
                const struct fp_image_frame_params& rhs);

#endif  // LIBEC_FINGERPRINT_FP_INFO_PARAMS_H_
