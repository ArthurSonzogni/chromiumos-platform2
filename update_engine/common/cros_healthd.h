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

#ifndef UPDATE_ENGINE_COMMON_CROS_HEALTHD_H_
#define UPDATE_ENGINE_COMMON_CROS_HEALTHD_H_

#include "update_engine/common/cros_healthd_interface.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

namespace chromeos_update_engine {

class CrosHealthd : public CrosHealthdInterface {
 public:
  CrosHealthd() = default;
  CrosHealthd(const CrosHealthd&) = delete;
  CrosHealthd& operator=(const CrosHealthd&) = delete;

  ~CrosHealthd() = default;

  // Bootstraps the mojo services. This can only be done once in each process.
  void BootstrapMojo();

  // CrosHealthdInterface overrides.
  TelemetryInfo* const GetTelemetryInfo() override;
  void ProbeTelemetryInfo(
      const std::unordered_set<TelemetryCategoryEnum>& categories,
      base::OnceClosure once_callback) override;

 private:
  FRIEND_TEST(CrosHealthdTest, ParseSystemResultCheck);
  FRIEND_TEST(CrosHealthdTest, ParseMemoryResultCheck);
  FRIEND_TEST(CrosHealthdTest, ParseNonRemovableBlockDeviceResultCheck);
  FRIEND_TEST(CrosHealthdTest, ParseCpuResultCheck);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckMissingBusResult);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckMissingBusInfo);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckPciBusDefault);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckPciBus);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckUsbBusDefault);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckUsbBus);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckThunderboltBusDefault);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckThunderboltBus);
  FRIEND_TEST(CrosHealthdTest, ParseBusResultCheckAllBus);

  void OnProbeTelemetryInfo(base::OnceClosure once_callback,
                            ash::cros_healthd::mojom::TelemetryInfoPtr result);

  // Parsing helpers for `OnProbTelemetryInfo()` .
  static bool ParseSystemResult(
      ash::cros_healthd::mojom::TelemetryInfoPtr* result,
      TelemetryInfo* telemetry_info);
  static bool ParseMemoryResult(
      ash::cros_healthd::mojom::TelemetryInfoPtr* result,
      TelemetryInfo* telemetry_info);
  static bool ParseNonRemovableBlockDeviceResult(
      ash::cros_healthd::mojom::TelemetryInfoPtr* result,
      TelemetryInfo* telemetry_info);
  static bool ParseCpuResult(ash::cros_healthd::mojom::TelemetryInfoPtr* result,
                             TelemetryInfo* telemetry_info);
  static bool ParseBusResult(ash::cros_healthd::mojom::TelemetryInfoPtr* result,
                             TelemetryInfo* telemetry_info);

  std::unique_ptr<TelemetryInfo> telemetry_info_;

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;

  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdProbeService>
      cros_healthd_probe_service_;

  base::WeakPtrFactory<CrosHealthd> weak_ptr_factory_{this};
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CROS_HEALTHD_H_
