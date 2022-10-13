// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"

#include <cstdint>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/logging.h>

#include "diagnostics/cros_healthd/routines/ac_power/ac_power.h"
#include "diagnostics/cros_healthd/routines/arc_dns_resolution/arc_dns_resolution.h"
#include "diagnostics/cros_healthd/routines/arc_http/arc_http.h"
#include "diagnostics/cros_healthd/routines/arc_ping/arc_ping.h"
#include "diagnostics/cros_healthd/routines/battery_capacity/battery_capacity.h"
#include "diagnostics/cros_healthd/routines/battery_charge/battery_charge.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge.h"
#include "diagnostics/cros_healthd/routines/battery_health/battery_health.h"
#include "diagnostics/cros_healthd/routines/captive_portal/captive_portal.h"
#include "diagnostics/cros_healthd/routines/cpu_cache/cpu_cache.h"
#include "diagnostics/cros_healthd/routines/cpu_stress/cpu_stress.h"
#include "diagnostics/cros_healthd/routines/disk_read/disk_read.h"
#include "diagnostics/cros_healthd/routines/dns_latency/dns_latency.h"
#include "diagnostics/cros_healthd/routines/dns_resolution/dns_resolution.h"
#include "diagnostics/cros_healthd/routines/dns_resolver_present/dns_resolver_present.h"
#include "diagnostics/cros_healthd/routines/fingerprint/fingerprint.h"
#include "diagnostics/cros_healthd/routines/fingerprint_alive/fingerprint_alive.h"
#include "diagnostics/cros_healthd/routines/floating_point/floating_point_accuracy.h"
#include "diagnostics/cros_healthd/routines/gateway_can_be_pinged/gateway_can_be_pinged.h"
#include "diagnostics/cros_healthd/routines/has_secure_wifi_connection/has_secure_wifi_connection.h"
#include "diagnostics/cros_healthd/routines/http_firewall/http_firewall.h"
#include "diagnostics/cros_healthd/routines/https_firewall/https_firewall.h"
#include "diagnostics/cros_healthd/routines/https_latency/https_latency.h"
#include "diagnostics/cros_healthd/routines/lan_connectivity/lan_connectivity.h"
#include "diagnostics/cros_healthd/routines/memory/memory.h"
#include "diagnostics/cros_healthd/routines/nvme_self_test/nvme_self_test.h"
#include "diagnostics/cros_healthd/routines/nvme_wear_level/nvme_wear_level.h"
#include "diagnostics/cros_healthd/routines/prime_search/prime_search.h"
#include "diagnostics/cros_healthd/routines/sensor/sensitive_sensor.h"
#include "diagnostics/cros_healthd/routines/signal_strength/signal_strength.h"
#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check.h"
#include "diagnostics/cros_healthd/routines/urandom/urandom.h"
#include "diagnostics/cros_healthd/routines/video_conferencing/video_conferencing.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

CrosHealthdRoutineFactoryImpl::CrosHealthdRoutineFactoryImpl(Context* context)
    : context_(context) {
  DCHECK(context_);

  parameter_fetcher_ =
      std::make_unique<RoutineParameterFetcher>(context_->cros_config());
}

CrosHealthdRoutineFactoryImpl::~CrosHealthdRoutineFactoryImpl() = default;

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeUrandomRoutine(
    ash::cros_healthd::mojom::NullableUint32Ptr length_seconds) {
  return CreateUrandomRoutine(length_seconds.is_null()
                                  ? std::nullopt
                                  : std::optional<base::TimeDelta>(
                                        base::Seconds(length_seconds->value)));
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryCapacityRoutine() {
  std::optional<uint32_t> low_mah;
  std::optional<uint32_t> high_mah;
  parameter_fetcher_->GetBatteryCapacityParameters(&low_mah, &high_mah);
  return CreateBatteryCapacityRoutine(context_, low_mah, high_mah);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryHealthRoutine() {
  std::optional<uint32_t> maximum_cycle_count;
  std::optional<uint8_t> percent_battery_wear_allowed;
  parameter_fetcher_->GetBatteryHealthParameters(&maximum_cycle_count,
                                                 &percent_battery_wear_allowed);
  return CreateBatteryHealthRoutine(context_, maximum_cycle_count,
                                    percent_battery_wear_allowed);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeSmartctlCheckRoutine() {
  return CreateSmartctlCheckRoutine();
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeAcPowerRoutine(
    ash::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const std::optional<std::string>& expected_power_type) {
  return std::make_unique<AcPowerRoutine>(expected_status, expected_power_type);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeCpuCacheRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  return CreateCpuCacheRoutine(exec_duration);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeCpuStressRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  return CreateCpuStressRoutine(exec_duration);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeFloatingPointAccuracyRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  return CreateFloatingPointAccuracyRoutine(exec_duration);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeNvmeWearLevelRoutine(
    org::chromium::debugdProxyInterface* debugd_proxy,
    ash::cros_healthd::mojom::NullableUint32Ptr wear_level_threshold) {
  DCHECK(debugd_proxy);
  std::optional<uint32_t> wear_level_threshold_ =
      !wear_level_threshold.is_null()
          ? wear_level_threshold->value
          : parameter_fetcher_->GetNvmeWearLevelParameters();
  return std::make_unique<NvmeWearLevelRoutine>(debugd_proxy,
                                                wear_level_threshold_);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeNvmeSelfTestRoutine(
    org::chromium::debugdProxyInterface* debugd_proxy,
    ash::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) {
  DCHECK(debugd_proxy);

  NvmeSelfTestRoutine::SelfTestType type =
      nvme_self_test_type ==
              ash::cros_healthd::mojom::NvmeSelfTestTypeEnum::kShortSelfTest
          ? NvmeSelfTestRoutine::kRunShortSelfTest
          : NvmeSelfTestRoutine::kRunLongSelfTest;

  return std::make_unique<NvmeSelfTestRoutine>(debugd_proxy, type);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeDiskReadRoutine(
    ash::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  return CreateDiskReadRoutine(type, exec_duration, file_size_mb);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakePrimeSearchRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  std::optional<uint64_t> max_num;
  parameter_fetcher_->GetPrimeSearchParameters(&max_num);
  return CreatePrimeSearchRoutine(exec_duration, max_num);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  return std::make_unique<BatteryDischargeRoutine>(
      context_, exec_duration, maximum_discharge_percent_allowed);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryChargeRoutine(
    base::TimeDelta exec_duration, uint32_t minimum_charge_percent_required) {
  return std::make_unique<BatteryChargeRoutine>(
      context_, exec_duration, minimum_charge_percent_required);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeMemoryRoutine() {
  return std::make_unique<MemoryRoutine>(context_);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeLanConnectivityRoutine() {
  return CreateLanConnectivityRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeSignalStrengthRoutine() {
  return CreateSignalStrengthRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeGatewayCanBePingedRoutine() {
  return CreateGatewayCanBePingedRoutine(
      context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeHasSecureWiFiConnectionRoutine() {
  return CreateHasSecureWiFiConnectionRoutine(
      context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeDnsResolverPresentRoutine() {
  return CreateDnsResolverPresentRoutine(
      context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeDnsLatencyRoutine() {
  return CreateDnsLatencyRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeDnsResolutionRoutine() {
  return CreateDnsResolutionRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeCaptivePortalRoutine() {
  return CreateCaptivePortalRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeHttpFirewallRoutine() {
  return CreateHttpFirewallRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeHttpsFirewallRoutine() {
  return CreateHttpsFirewallRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeHttpsLatencyRoutine() {
  return CreateHttpsLatencyRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname) {
  return CreateVideoConferencingRoutine(
      stun_server_hostname, context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeArcHttpRoutine() {
  return CreateArcHttpRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeArcPingRoutine() {
  return CreateArcPingRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeArcDnsResolutionRoutine() {
  return CreateArcDnsResolutionRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeSensitiveSensorRoutine() {
  return std::make_unique<SensitiveSensorRoutine>(context_->mojo_service());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeFingerprintRoutine() {
  auto params = parameter_fetcher_->GetFingerprintParameters();

  return std::make_unique<FingerprintRoutine>(context_, std::move(params));
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeFingerprintAliveRoutine() {
  return std::make_unique<FingerprintAliveRoutine>(context_);
}

}  // namespace diagnostics
