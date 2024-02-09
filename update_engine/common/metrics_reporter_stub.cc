// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/metrics_reporter_stub.h"

#include <memory>

namespace chromeos_update_engine {

namespace metrics {

std::unique_ptr<MetricsReporterInterface> CreateMetricsReporter() {
  return std::make_unique<MetricsReporterStub>();
}

}  // namespace metrics

}  // namespace chromeos_update_engine
