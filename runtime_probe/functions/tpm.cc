// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/tpm.h"

#include <cstddef>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/no_destructor.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

namespace cros_healthd_mojom = ::ash::cros_healthd::mojom;

namespace {

constexpr char kUnknownVendorSpecific[] = "unknown";

// Simulator Vendor ID ("SIMU").
constexpr uint32_t kVendorIdSimulator = 0x53494d55;

// Callback function to convert the telemetry info to |probe_result|.
void ProbeTpmTelemetryInfoCallback(
    base::OnceCallback<void(TpmFunction::DataType)> callback,
    cros_healthd_mojom::TelemetryInfoPtr telemetry_info_ptr) {
  const auto& tpm_result = telemetry_info_ptr->tpm_result;
  if (tpm_result.is_null()) {
    LOG(ERROR) << "No TPM result from cros_healthd.";
    std::move(callback).Run(TpmFunction::DataType{});
  }

  TpmFunction::DataType probe_result;
  if (tpm_result->is_error()) {
    const auto& error = tpm_result->get_error();
    LOG(ERROR) << "Got an error when fetching TPM info: " << error->type
               << "::" << error->msg;
  } else {
    const auto& tpm_version = tpm_result->get_tpm_info()->version;
    // Filter TPM2 simulator
    if (!tpm_version.is_null() &&
        tpm_version->manufacturer != kVendorIdSimulator) {
      probe_result.Append(
          base::Value::Dict()
              .Set("spec_level", std::to_string(tpm_version->spec_level))
              .Set("vendor_specific", tpm_version->vendor_specific.value_or(
                                          kUnknownVendorSpecific))
              .Set("manufacturer",
                   base::StringPrintf("0x%x", tpm_version->manufacturer)));
    }
  }
  std::move(callback).Run(std::move(probe_result));
}

}  // namespace

void TpmFunction::EvalAsyncImpl(
    base::OnceCallback<void(TpmFunction::DataType)> callback) const {
  Context::Get()->GetCrosHealthdProbeServiceProxy()->ProbeTelemetryInfo(
      {cros_healthd_mojom::ProbeCategoryEnum::kTpm},
      base::BindOnce(&ProbeTpmTelemetryInfoCallback, std::move(callback)));
}

}  // namespace runtime_probe
