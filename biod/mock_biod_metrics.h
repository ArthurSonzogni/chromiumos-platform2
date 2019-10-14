// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_BIOD_METRICS_H_
#define BIOD_MOCK_BIOD_METRICS_H_

#include <gmock/gmock.h>

#include "biod/biod_metrics.h"

namespace biod {
namespace metrics {

class MockBiodMetrics : public BiodMetricsInterface {
 public:
  ~MockBiodMetrics() override = default;

  MOCK_METHOD(bool, SendEnrolledFingerCount, (int finger_count), (override));
  MOCK_METHOD(bool, SendFpUnlockEnabled, (bool enabled), (override));
  MOCK_METHOD(bool,
              SendFpLatencyStats,
              (bool matched, int capture_ms, int match_ms, int overall_ms),
              (override));
  MOCK_METHOD(bool,
              SendFwUpdaterStatus,
              (FwUpdaterStatus status,
               updater::UpdateReason reason,
               int overall_ms),
              (override));
  MOCK_METHOD(bool,
              SendIgnoreMatchEventOnPowerButtonPress,
              (bool is_ignored),
              (override));
  MOCK_METHOD(bool, SendResetContextMode, (const FpMode& mode), (override));
};

}  // namespace metrics
}  // namespace biod

#endif  // BIOD_MOCK_BIOD_METRICS_H_
