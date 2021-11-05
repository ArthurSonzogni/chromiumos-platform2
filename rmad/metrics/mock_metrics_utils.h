// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_METRICS_MOCK_METRICS_UTILS_H_
#define RMAD_METRICS_MOCK_METRICS_UTILS_H_

#include "rmad/metrics/metrics_utils.h"

#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>

#include "rmad/utils/json_store.h"

namespace rmad {

class MockMetricsUtils : public MetricsUtils {
 public:
  MockMetricsUtils() = default;
  ~MockMetricsUtils() override = default;

  MOCK_METHOD(bool, Record, (scoped_refptr<JsonStore>, bool), (override));
};

}  // namespace rmad

#endif  // RMAD_METRICS_MOCK_METRICS_UTILS_H_
