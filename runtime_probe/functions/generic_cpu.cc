// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_cpu.h"

#include <unordered_set>
#include <utility>

#include <base/functional/bind.h>
#include <base/no_destructor.h>
#include <base/values.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

namespace cros_healthd_mojom = ::ash::cros_healthd::mojom;

namespace {

constexpr char kUnknownModel[] = "unknown";

// Count the number of distinct |core_id| in |PhysicalCpuInfo| to get the
// number of cores.
int CountLogicalCores(
    const cros_healthd_mojom::PhysicalCpuInfoPtr& physical_cpu_info) {
  std::unordered_set<uint32_t> core_ids;
  for (const auto& logical_cpu : physical_cpu_info->logical_cpus) {
    core_ids.insert(logical_cpu->core_id);
  }
  return core_ids.size();
}

// Callback function to convert the telemetry info to |probe_result|.
void ProbeCpuTelemetryInfoCallback(
    base::OnceCallback<void(GenericCpuFunction::DataType)> callback,
    cros_healthd_mojom::TelemetryInfoPtr telemetry_info_ptr) {
  const auto& cpu_result = telemetry_info_ptr->cpu_result;
  if (cpu_result.is_null()) {
    LOG(ERROR) << "No CPU result from cros_healthd.";
    std::move(callback).Run(GenericCpuFunction::DataType{});
  }

  GenericCpuFunction::DataType probe_result;
  if (cpu_result->is_error()) {
    const auto& error = cpu_result->get_error();
    LOG(ERROR) << "Got an error when fetching CPU info: " << error->type
               << "::" << error->msg;
  } else {
    const auto& cpu_info = cpu_result->get_cpu_info();
    for (const auto& physical_cpu_info : cpu_info->physical_cpus) {
      // TODO(b/285999787): include chip_id in the probe result.
      probe_result.Append(
          base::Value::Dict()
              .Set("cores", CountLogicalCores(physical_cpu_info))
              .Set("model",
                   physical_cpu_info->model_name.value_or(kUnknownModel)));
    }
  }
  std::move(callback).Run(std::move(probe_result));
}

}  // namespace

void GenericCpuFunction::EvalAsyncImpl(
    base::OnceCallback<void(GenericCpuFunction::DataType)> callback) const {
  Context::Get()->GetCrosHealthdProbeServiceProxy()->ProbeTelemetryInfo(
      {cros_healthd_mojom::ProbeCategoryEnum::kCpu},
      base::BindOnce(&ProbeCpuTelemetryInfoCallback, std::move(callback)));
}

}  // namespace runtime_probe
