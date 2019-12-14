// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/optional.h>
#include <dbus/cros_healthd/dbus-constants.h>
#include <mojo/edk/embedder/embedder.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "diagnostics/cros_healthd/utils/cpu_utils.h"
#include "diagnostics/cros_healthd/utils/disk_utils.h"
#include "diagnostics/cros_healthd/utils/vpd_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    BatteryFetcher* battery_fetcher, CrosHealthdRoutineService* routine_service)
    : battery_fetcher_(battery_fetcher), routine_service_(routine_service) {
  DCHECK(battery_fetcher_);
  DCHECK(routine_service_);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::GetAvailableRoutines(
    const GetAvailableRoutinesCallback& callback) {
  callback.Run(routine_service_->GetAvailableRoutines());
}

void CrosHealthdMojoService::GetRoutineUpdate(
    int32_t id,
    chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
    bool include_output,
    const GetRoutineUpdateCallback& callback) {
  chromeos::cros_healthd::mojom::RoutineUpdate response{
      0, mojo::ScopedHandle(),
      chromeos::cros_healthd::mojom::RoutineUpdateUnion::New()};
  routine_service_->GetRoutineUpdate(id, command, include_output, &response);
  callback.Run(chromeos::cros_healthd::mojom::RoutineUpdate::New(
      response.progress_percent, std::move(response.output),
      std::move(response.routine_update_union)));
}

void CrosHealthdMojoService::RunUrandomRoutine(
    uint32_t length_seconds, const RunUrandomRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunUrandomRoutine(length_seconds, &response.id,
                                      &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryCapacityRoutine(
    uint32_t low_mah,
    uint32_t high_mah,
    const RunBatteryCapacityRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryCapacityRoutine(low_mah, high_mah, &response.id,
                                              &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed,
    const RunBatteryHealthRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryHealthRoutine(maximum_cycle_count,
                                            percent_battery_wear_allowed,
                                            &response.id, &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunSmartctlCheckRoutine(
    const RunSmartctlCheckRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunSmartctlCheckRoutine(&response.id, &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::ProbeTelemetryInfo(
    const std::vector<ProbeCategoryEnum>& categories,
    const ProbeTelemetryInfoCallback& callback) {
  chromeos::cros_healthd::mojom::TelemetryInfo telemetry_info;
  for (const auto category : categories) {
    switch (category) {
      case ProbeCategoryEnum::kBattery: {
        auto batteries = battery_fetcher_->FetchBatteryInfo();
        telemetry_info.battery_info.Swap(&batteries[0]);
        break;
      }
      case ProbeCategoryEnum::kCachedVpdData: {
        auto vpd_info = FetchCachedVpdInfo(base::FilePath("/"));
        telemetry_info.vpd_info.Swap(&vpd_info);
        break;
      }
      case ProbeCategoryEnum::kCpu: {
        telemetry_info.cpu_info = FetchCpuInfo(base::FilePath("/"));
        break;
      }
      case ProbeCategoryEnum::kNonRemovableBlockDevices: {
        telemetry_info.block_device_info = base::Optional<std::vector<
            chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr>>(
            FetchNonRemovableBlockDevicesInfo(base::FilePath("/")));
        break;
      }
    }
  }

  callback.Run(telemetry_info.Clone());
}

void CrosHealthdMojoService::AddBinding(
    chromeos::cros_healthd::mojom::CrosHealthdServiceRequest request) {
  binding_set_.AddBinding(this /* impl */, std::move(request));
}

}  // namespace diagnostics
