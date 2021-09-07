// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_IMPL_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/optional.h>

#include "diagnostics/cros_healthd/cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/routine_parameter_fetcher.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

// Production implementation of the CrosHealthdRoutineFactory interface.
class CrosHealthdRoutineFactoryImpl final : public CrosHealthdRoutineFactory {
 public:
  explicit CrosHealthdRoutineFactoryImpl(Context* context);
  CrosHealthdRoutineFactoryImpl(const CrosHealthdRoutineFactoryImpl&) = delete;
  CrosHealthdRoutineFactoryImpl& operator=(
      const CrosHealthdRoutineFactoryImpl&) = delete;
  ~CrosHealthdRoutineFactoryImpl() override;

  // CrosHealthdRoutineFactory overrides:
  std::unique_ptr<DiagnosticRoutine> MakeUrandomRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds) override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryCapacityRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryHealthRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeSmartctlCheckRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type) override;
  std::unique_ptr<DiagnosticRoutine> MakeCpuCacheRoutine(
      const base::Optional<base::TimeDelta>& exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeCpuStressRoutine(
      const base::Optional<base::TimeDelta>& exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeFloatingPointAccuracyRoutine(
      const base::Optional<base::TimeDelta>& exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeNvmeWearLevelRoutine(
      DebugdAdapter* debugd_adapter, uint32_t wear_level_threshold) override;
  std::unique_ptr<DiagnosticRoutine> MakeNvmeSelfTestRoutine(
      DebugdAdapter* debugd_adapter,
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type)
      override;
  std::unique_ptr<DiagnosticRoutine> MakeDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb) override;
  std::unique_ptr<DiagnosticRoutine> MakePrimeSearchRoutine(
      const base::Optional<base::TimeDelta>& exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed) override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryChargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t minimum_charge_percent_required) override;
  std::unique_ptr<DiagnosticRoutine> MakeMemoryRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeLanConnectivityRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeSignalStrengthRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeGatewayCanBePingedRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeHasSecureWiFiConnectionRoutine()
      override;
  std::unique_ptr<DiagnosticRoutine> MakeDnsResolverPresentRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeDnsLatencyRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeDnsResolutionRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeCaptivePortalRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeHttpFirewallRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeHttpsFirewallRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeHttpsLatencyRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeVideoConferencingRoutine(
      const base::Optional<std::string>& stun_server_hostname) override;
  std::unique_ptr<DiagnosticRoutine> MakeArcHttpRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeArcPingRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeArcDnsResolutionRoutine() override;

 private:
  // Unowned pointer that should outlive this instance.
  Context* const context_ = nullptr;

  // Used to fetch default parameters for routines.
  std::unique_ptr<RoutineParameterFetcher> parameter_fetcher_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_IMPL_H_
