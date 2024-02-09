// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_FAKE_CROS_HEALTHD_H_
#define UPDATE_ENGINE_COMMON_FAKE_CROS_HEALTHD_H_

#include "update_engine/common/cros_healthd_interface.h"

#include <memory>
#include <unordered_set>
#include <utility>

namespace chromeos_update_engine {

class FakeCrosHealthd : public CrosHealthdInterface {
 public:
  // CrosHealthdInterface overrides.
  TelemetryInfo* const GetTelemetryInfo() { return telemetry_info_.get(); }
  void ProbeTelemetryInfo(
      const std::unordered_set<TelemetryCategoryEnum>& categories,
      base::OnceClosure once_callback) {
    std::move(once_callback).Run();
  }

  std::unique_ptr<TelemetryInfo>& telemetry_info() { return telemetry_info_; }

 private:
  std::unique_ptr<TelemetryInfo> telemetry_info_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_FAKE_CROS_HEALTHD_H_
