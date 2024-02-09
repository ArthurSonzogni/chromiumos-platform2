// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/cros_healthd_stub.h"

#include <memory>
#include <unordered_set>
#include <utility>

namespace chromeos_update_engine {

std::unique_ptr<CrosHealthdInterface> CreateCrosHealthd() {
  return std::make_unique<CrosHealthdStub>();
}

TelemetryInfo* const CrosHealthdStub::GetTelemetryInfo() {
  return nullptr;
}

void CrosHealthdStub::ProbeTelemetryInfo(
    const std::unordered_set<TelemetryCategoryEnum>& categories,
    base::OnceClosure once_callback) {
  std::move(once_callback).Run();
}

}  // namespace chromeos_update_engine
