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

#ifndef UPDATE_ENGINE_COMMON_CROS_HEALTHD_STUB_H_
#define UPDATE_ENGINE_COMMON_CROS_HEALTHD_STUB_H_

#include "update_engine/common/cros_healthd_interface.h"

#include <unordered_set>

namespace chromeos_update_engine {

// An implementation of the CrosHealthdInterface that does nothing.
class CrosHealthdStub : public CrosHealthdInterface {
 public:
  CrosHealthdStub() = default;
  CrosHealthdStub(const CrosHealthdStub&) = delete;
  CrosHealthdStub& operator=(const CrosHealthdStub&) = delete;

  ~CrosHealthdStub() = default;

  // CrosHealthdInterface overrides.
  TelemetryInfo* const GetTelemetryInfo() override;
  void ProbeTelemetryInfo(
      const std::unordered_set<TelemetryCategoryEnum>& categories,
      base::OnceClosure once_callback) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CROS_HEALTHD_STUB_H_
