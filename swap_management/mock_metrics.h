// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_MOCK_METRICS_H_
#define SWAP_MANAGEMENT_MOCK_METRICS_H_

#include "swap_management/metrics.h"

#include <gmock/gmock.h>

namespace swap_management {
class MockMetrics : public swap_management::Metrics {
 public:
  MockMetrics() = default;
  MockMetrics& operator=(const MockMetrics&) = delete;
  MockMetrics(const MockMetrics&) = delete;

  MOCK_METHOD(void, Start, (), (override));
};
}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_MOCK_METRICS_H_
