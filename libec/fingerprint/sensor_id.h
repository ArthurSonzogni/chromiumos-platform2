// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_SENSOR_ID_H_
#define LIBEC_FINGERPRINT_SENSOR_ID_H_

#include <cstdint>

struct SensorId {
  uint32_t vendor_id = 0;
  uint32_t product_id = 0;
  uint32_t model_id = 0;
  uint32_t version = 0;

  friend bool operator==(const SensorId&, const SensorId&) = default;
};

#endif  // LIBEC_FINGERPRINT_SENSOR_ID_H_
