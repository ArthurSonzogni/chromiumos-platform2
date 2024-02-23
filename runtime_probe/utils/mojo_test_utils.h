// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_MOJO_TEST_UTILS_H_
#define RUNTIME_PROBE_UTILS_MOJO_TEST_UTILS_H_

#include <vector>

#include <base/check.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

namespace runtime_probe {

namespace cros_healthd_mojom = ::ash::cros_healthd::mojom;

// Fake CrosHealthdProbeService for testing.
class FakeCrosHealthdProbeService
    : public cros_healthd_mojom::CrosHealthdProbeService {
 public:
  void SetTpmResult(cros_healthd_mojom::TpmResultPtr tpm_result);
  void SetCpuResult(cros_healthd_mojom::CpuResultPtr cpu_result);

 private:
  // cros_healthd_mojom::CrosHealthdProbeService overrides:
  void ProbeTelemetryInfo(
      const std::vector<cros_healthd_mojom::ProbeCategoryEnum>&,
      ProbeTelemetryInfoCallback callback) override;
  void ProbeProcessInfo(uint32_t process_id,
                        ProbeProcessInfoCallback callback) override;
  void ProbeMultipleProcessInfo(
      const std::optional<std::vector<uint32_t>>& process_ids,
      bool ignore_single_process_error,
      ProbeMultipleProcessInfoCallback callback) override;

  cros_healthd_mojom::TelemetryInfoPtr telemetry_info_ptr_{
      cros_healthd_mojom::TelemetryInfo::New()};
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_MOJO_TEST_UTILS_H_
