// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
