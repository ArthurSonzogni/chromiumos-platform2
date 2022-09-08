// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_DIAG_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_DIAG_CONSTANTS_H_

#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace diagnostics {

// Used for printing and parsing routine names.
constexpr struct {
  const char* switch_name;
  mojo_ipc::DiagnosticRoutineEnum routine;
} kDiagnosticRoutineSwitches[] = {
    {"battery_capacity", mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity},
    {"battery_health", mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth},
    {"urandom", mojo_ipc::DiagnosticRoutineEnum::kUrandom},
    {"smartctl_check", mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck},
    {"ac_power", mojo_ipc::DiagnosticRoutineEnum::kAcPower},
    {"cpu_cache", mojo_ipc::DiagnosticRoutineEnum::kCpuCache},
    {"cpu_stress", mojo_ipc::DiagnosticRoutineEnum::kCpuStress},
    {"floating_point_accuracy",
     mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy},
    {"nvme_wear_level", mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel},
    {"nvme_self_test", mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest},
    {"disk_read", mojo_ipc::DiagnosticRoutineEnum::kDiskRead},
    {"prime_search", mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch},
    {"battery_discharge", mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge},
    {"battery_charge", mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge},
    {"memory", mojo_ipc::DiagnosticRoutineEnum::kMemory},
    {"lan_connectivity", mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity},
    {"signal_strength", mojo_ipc::DiagnosticRoutineEnum::kSignalStrength},
    {"gateway_can_be_pinged",
     mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged},
    {"has_secure_wifi_connection",
     mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection},
    {"dns_resolver_present",
     mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent},
    {"dns_latency", mojo_ipc::DiagnosticRoutineEnum::kDnsLatency},
    {"dns_resolution", mojo_ipc::DiagnosticRoutineEnum::kDnsResolution},
    {"captive_portal", mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal},
    {"http_firewall", mojo_ipc::DiagnosticRoutineEnum::kHttpFirewall},
    {"https_firewall", mojo_ipc::DiagnosticRoutineEnum::kHttpsFirewall},
    {"https_latency", mojo_ipc::DiagnosticRoutineEnum::kHttpsLatency},
    {"video_conferencing", mojo_ipc::DiagnosticRoutineEnum::kVideoConferencing},
    {"arc_http", mojo_ipc::DiagnosticRoutineEnum::kArcHttp},
    {"arc_ping", mojo_ipc::DiagnosticRoutineEnum::kArcPing},
    {"arc_dns_resolution", mojo_ipc::DiagnosticRoutineEnum::kArcDnsResolution}};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_DIAG_CONSTANTS_H_
