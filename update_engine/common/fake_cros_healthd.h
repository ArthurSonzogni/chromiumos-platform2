//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

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
