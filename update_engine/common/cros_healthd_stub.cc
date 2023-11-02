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
