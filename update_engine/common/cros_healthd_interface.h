//
// Copyright (C) 2021 The Android Open Source Project
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

#ifndef UPDATE_ENGINE_COMMON_CROS_HEALTHD_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_CROS_HEALTHD_INTERFACE_H_

#include <memory>
#include <unordered_set>

#include <base/functional/callback.h>

#include "update_engine/common/telemetry_info.h"

namespace chromeos_update_engine {

// The abstract cros_healthd interface defines the interaction with the
// platform's cros_healthd.
class CrosHealthdInterface {
 public:
  CrosHealthdInterface(const CrosHealthdInterface&) = delete;
  CrosHealthdInterface& operator=(const CrosHealthdInterface&) = delete;

  virtual ~CrosHealthdInterface() = default;

  // Returns the cached telemetry info from the last succeeded request.
  // `nullptr` will be returned if no request has been sent or the last request
  // failed.
  virtual TelemetryInfo* const GetTelemetryInfo() = 0;

  // Probes the telemetry info from cros_healthd and caches the results. Limited
  // to `TelemetryInfo` as the avaiable telemetry is vast. `once_callback` will
  // be called when the request is finished.
  virtual void ProbeTelemetryInfo(
      const std::unordered_set<TelemetryCategoryEnum>& categories,
      base::OnceClosure once_callback) = 0;

 protected:
  CrosHealthdInterface() = default;
};

// This factory function creates a new CrosHealthdInterface instance for the
// current platform.
std::unique_ptr<CrosHealthdInterface> CreateCrosHealthd();

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CROS_HEALTHD_INTERFACE_H_
