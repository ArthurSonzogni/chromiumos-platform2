// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_DIAGNOSTICS_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_DIAGNOSTICS_SERVICE_H_

#include <cstdint>
#include <string>
#include <vector>

#include <base/macros.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/system/handle.h>

#include "diagnostics/wilco_dtc_supportd/routine_service.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Helper class that allows testing of the routine service.
class FakeDiagnosticsService final
    : public RoutineService::Delegate,
      public chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService {
 public:
  FakeDiagnosticsService();
  FakeDiagnosticsService(const FakeDiagnosticsService&) = delete;
  FakeDiagnosticsService& operator=(const FakeDiagnosticsService&) = delete;

  ~FakeDiagnosticsService();

  // RoutineService::Delegate overrides:
  bool GetCrosHealthdDiagnosticsService(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService> service)
      override;

  // chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService overrides:
  void GetAvailableRoutines(GetAvailableRoutinesCallback callback) override;
  void GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output,
      GetRoutineUpdateCallback callback) override;
  void RunUrandomRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunUrandomRoutineCallback callback) override;
  void RunBatteryCapacityRoutine(
      RunBatteryCapacityRoutineCallback callback) override;
  void RunBatteryHealthRoutine(
      RunBatteryHealthRoutineCallback callback) override;
  void RunSmartctlCheckRoutine(
      RunSmartctlCheckRoutineCallback callback) override;
  void RunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type,
      RunAcPowerRoutineCallback callback) override;
  void RunCpuCacheRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunCpuCacheRoutineCallback callback) override;
  void RunCpuStressRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunCpuStressRoutineCallback callback) override;
  void RunFloatingPointAccuracyRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunFloatingPointAccuracyRoutineCallback callback) override;
  void RunNvmeWearLevelRoutine(
      uint32_t wear_level_threshold,
      RunNvmeWearLevelRoutineCallback callback) override;
  void RunNvmeSelfTestRoutine(
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type,
      RunNvmeSelfTestRoutineCallback callback) override;
  void RunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      uint32_t length_seconds,
      uint32_t file_size_mb,
      RunDiskReadRoutineCallback callback) override;
  void RunPrimeSearchRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunPrimeSearchRoutineCallback callback) override;
  void RunBatteryDischargeRoutine(
      uint32_t length_seconds,
      uint32_t maximum_discharge_percent_allowed,
      RunBatteryDischargeRoutineCallback callback) override;
  void RunBatteryChargeRoutine(
      uint32_t length_seconds,
      uint32_t minimum_charge_percent_required,
      RunBatteryChargeRoutineCallback callback) override;
  void RunMemoryRoutine(RunMemoryRoutineCallback callback) override;
  void RunLanConnectivityRoutine(RunLanConnectivityRoutineCallback) override;
  void RunSignalStrengthRoutine(RunSignalStrengthRoutineCallback) override;
  void RunGatewayCanBePingedRoutine(
      RunGatewayCanBePingedRoutineCallback) override;
  void RunHasSecureWiFiConnectionRoutine(
      RunHasSecureWiFiConnectionRoutineCallback) override;
  void RunDnsResolverPresentRoutine(
      RunDnsResolverPresentRoutineCallback) override;
  void RunDnsLatencyRoutine(RunDnsLatencyRoutineCallback) override;
  void RunDnsResolutionRoutine(RunDnsResolutionRoutineCallback) override;
  void RunCaptivePortalRoutine(RunCaptivePortalRoutineCallback) override;
  void RunHttpFirewallRoutine(RunHttpFirewallRoutineCallback) override;
  void RunHttpsFirewallRoutine(RunHttpsFirewallRoutineCallback) override;
  void RunHttpsLatencyRoutine(RunHttpsLatencyRoutineCallback) override;
  void RunVideoConferencingRoutine(
      const base::Optional<std::string>& stun_server_hostname,
      RunVideoConferencingRoutineCallback) override;
  void RunArcHttpRoutine(RunArcHttpRoutineCallback) override;
  void RunArcPingRoutine(RunArcPingRoutineCallback) override;
  void RunArcDnsResolutionRoutine(RunArcDnsResolutionRoutineCallback) override;

  // Overrides the default behavior of GetCrosHealthdDiagnosticsService to test
  // situations where mojo methods were called prior to wilco_dtc_supportd's
  // mojo service being established.
  void SetMojoServiceIsAvailable(bool is_available);

  // Overrides the default behavior of GetCrosHealthdDiagnosticsService to test
  // situations where cros_healthd is unresponsive.
  void SetMojoServiceIsResponsive(bool is_responsive);

  // Resets the mojo connection.
  void ResetMojoConnection();

  // Sets the response to any GetAvailableRoutines IPCs received.
  void SetGetAvailableRoutinesResponse(
      const std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>&
          available_routines);

  // Sets an interactive response to any GetRoutineUpdate IPCs received.
  void SetInteractiveUpdate(
      chromeos::cros_healthd::mojom::DiagnosticRoutineUserMessageEnum
          user_message,
      uint32_t progress_percent,
      const std::string& output);

  // Sets a noninteractive response to any GetRoutineUpdate IPCs received.
  void SetNonInteractiveUpdate(
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status,
      const std::string& status_message,
      uint32_t progress_percent,
      const std::string& output);

  // Sets the response to any RunSomeRoutine IPCs received.
  void SetRunSomeRoutineResponse(
      uint32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status);

 private:
  mojo::Receiver<chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService>
      service_receiver_{this /* impl */};

  // Used as the return value for any GetAvailableRoutines IPCs received.
  std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
      available_routines_;
  // Used as the return value for any GetRoutineUpdate IPCs received.
  chromeos::cros_healthd::mojom::RoutineUpdate routine_update_response_{
      0, mojo::ScopedHandle(),
      chromeos::cros_healthd::mojom::RoutineUpdateUnion::New()};
  // Used as the return value for any RunSomeRoutine IPCs received.
  chromeos::cros_healthd::mojom::RunRoutineResponse run_routine_response_;

  // Determines whether or not the service should present itself as available.
  bool is_available_ = true;
  // Determines whether or not the service should present itself as responsive.
  bool is_responsive_ = true;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_DIAGNOSTICS_SERVICE_H_
