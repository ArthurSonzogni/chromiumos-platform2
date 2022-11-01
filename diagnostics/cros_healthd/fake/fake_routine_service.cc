// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_routine_service.h"

#include <optional>

#include <base/logging.h>
#include <base/notreached.h>

#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

FakeRoutineService::FakeRoutineService() = default;
FakeRoutineService::~FakeRoutineService() = default;

void FakeRoutineService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::GetRoutineUpdate(
    int32_t id,
    mojo_ipc::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunUrandomRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunUrandomRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryCapacityRoutine(
    RunBatteryCapacityRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryHealthRoutine(
    RunBatteryHealthRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunSmartctlCheckRoutine(
    RunSmartctlCheckRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunAcPowerRoutine(
    mojo_ipc::AcPowerStatusEnum expected_status,
    const std::optional<std::string>& expected_power_type,
    RunAcPowerRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunCpuCacheRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunCpuCacheRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunCpuStressRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunCpuStressRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunFloatingPointAccuracyRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunFloatingPointAccuracyRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::DEPRECATED_RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold, RunNvmeWearLevelRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunNvmeWearLevelRoutine(
    ash::cros_healthd::mojom::NullableUint32Ptr wear_level_threshold,
    RunNvmeWearLevelRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunNvmeSelfTestRoutine(
    mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type,
    RunNvmeSelfTestRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    RunDiskReadRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunPrimeSearchRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunPrimeSearchRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    RunBatteryDischargeRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryChargeRoutine(
    uint32_t length_seconds,
    uint32_t minimum_charge_percent_required,
    RunBatteryDischargeRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunMemoryRoutine(RunMemoryRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunLanConnectivityRoutine(
    RunLanConnectivityRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunSignalStrengthRoutine(
    RunSignalStrengthRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunGatewayCanBePingedRoutine(
    RunGatewayCanBePingedRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunHasSecureWiFiConnectionRoutine(
    RunHasSecureWiFiConnectionRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDnsResolverPresentRoutine(
    RunDnsResolverPresentRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDnsLatencyRoutine(
    RunDnsLatencyRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDnsResolutionRoutine(
    RunDnsResolutionRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunCaptivePortalRoutine(
    RunCaptivePortalRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunHttpFirewallRoutine(
    RunHttpFirewallRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunHttpsFirewallRoutine(
    RunHttpsFirewallRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunHttpsLatencyRoutine(
    RunHttpsLatencyRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname,
    RunVideoConferencingRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunArcHttpRoutine(RunArcHttpRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunArcPingRoutine(RunArcPingRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunArcDnsResolutionRoutine(
    RunArcDnsResolutionRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunSensitiveSensorRoutine(
    RunSensitiveSensorRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunFingerprintRoutine(
    RunFingerprintRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunFingerprintAliveRoutine(
    RunFingerprintAliveRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunPrivacyScreenRoutine(
    bool target_state, RunPrivacyScreenRoutineCallback callback) {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
