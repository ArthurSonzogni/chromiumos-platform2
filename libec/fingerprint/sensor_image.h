// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_SENSOR_IMAGE_H_
#define LIBEC_FINGERPRINT_SENSOR_IMAGE_H_

#include <cstdint>

struct SensorImage {
  int width = 0;
  int height = 0;
  uint32_t frame_size = 0;
  uint32_t pixel_format = 0;
  uint16_t bpp = 0;

  friend bool operator==(const SensorImage&, const SensorImage&) = default;
};

#endif  // LIBEC_FINGERPRINT_SENSOR_IMAGE_H_
