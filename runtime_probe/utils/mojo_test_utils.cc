// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/mojo_test_utils.h"

#include <utility>
#include <vector>

#include <base/check.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

namespace runtime_probe {

void FakeCrosHealthdProbeService::ProbeTelemetryInfo(
    const std::vector<cros_healthd_mojom::ProbeCategoryEnum>&,
    ProbeTelemetryInfoCallback callback) {
  std::move(callback).Run(std::move(telemetry_info_ptr_));
}

void FakeCrosHealthdProbeService::ProbeProcessInfo(
    uint32_t process_id, ProbeProcessInfoCallback callback) {
  NOTIMPLEMENTED();
}

void FakeCrosHealthdProbeService::ProbeMultipleProcessInfo(
    const std::optional<std::vector<uint32_t>>& process_ids,
    bool ignore_single_process_error,
    ProbeMultipleProcessInfoCallback callback) {
  NOTIMPLEMENTED();
}

void FakeCrosHealthdProbeService::SetTpmResult(
    cros_healthd_mojom::TpmResultPtr tpm_result) {
  telemetry_info_ptr_->tpm_result = std::move(tpm_result);
}

void FakeCrosHealthdProbeService::SetCpuResult(
    cros_healthd_mojom::CpuResultPtr cpu_result) {
  telemetry_info_ptr_->cpu_result = std::move(cpu_result);
}

}  // namespace runtime_probe
