// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"

namespace org::chromium {
class debugdProxyInterface;
}  // namespace org::chromium

namespace diagnostics {

// Interface for constructing DiagnosticRoutines.
class CrosHealthdRoutineFactory {
 public:
  virtual ~CrosHealthdRoutineFactory() = default;

  // Constructs a new instance of the urandom routine. See
  // diagnostics/cros_healthd/routines/memory_and_cpu/urandom.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeUrandomRoutine(
      ash::cros_healthd::mojom::NullableUint32Ptr length_seconds) = 0;
  // Constructs a new instance of the battery capacity routine. See
  // diagnostics/cros_healthd/routines/battery_and_power/battery_capacity.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryCapacityRoutine() = 0;
  // Constructs a new instance of the battery health routine. See
  // diagnostics/cros_healthd/routines/battery_and_power/battery_health.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryHealthRoutine() = 0;
  // Constructs a new instance of the smartctl check routine. See
  // diagnostics/cros_healthd/routines/storage/smartctl_check.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeSmartctlCheckRoutine(
      org::chromium::debugdProxyInterface* debugd_proxy,
      ash::cros_healthd::mojom::NullableUint32Ptr
          percentage_used_threshold) = 0;
  // Constructs a new instance of the AC power routine. See
  // diagnostics/cros_healthd/routines/battery_and_power/ac_power.h for details
  // on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeAcPowerRoutine(
      ash::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const std::optional<std::string>& expected_power_type) = 0;
  // Constructs a new instance of the nvme_wear_level routine. See
  // diagnostics/cros_healthd/routines/storage/nvme_wear_level.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeNvmeWearLevelRoutine(
      org::chromium::debugdProxyInterface* debugd_proxy,
      ash::cros_healthd::mojom::NullableUint32Ptr wear_level_threshold) = 0;
  // Constructs a new instance of the NvmeSelfTest routine. See
  // diagnostics/cros_healthd/routines/storage/nvme_self_test.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeNvmeSelfTestRoutine(
      org::chromium::debugdProxyInterface* debugd_proxy,
      ash::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) = 0;
  // Constructs a new instance of the battery discharge routine. See
  // diagnostics/cros_healthd/routines/battery_and_power/battery_discharge.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed) = 0;
  // Constructs a new instance of the battery charge routine. See
  // diagnostics/cros_healthd/routines/battery_and_power/battery_charge.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryChargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t minimum_charge_percent_required) = 0;
  // Constructs a new instance of the LAN connectivity routine. See
  // diagnostics/cros_healthd/routines/network/lan_connectivity.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeLanConnectivityRoutine() = 0;
  // Constructs a new instance of the signal strength routine. See
  // diagnostics/cros_healthd/routines/network/signal_strength.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeSignalStrengthRoutine() = 0;
  // Constructs a new instance of the gateway can be pinged routine. See
  // diagnostics/cros_healthd/routines/network/gateway_can_be_pinged.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine>
  MakeGatewayCanBePingedRoutine() = 0;
  // Constructs a new instance of the has secure wifi connection routine. See
  // diagnostics/cros_healthd/routines/network/has_secure_wifi_connection.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine>
  MakeHasSecureWiFiConnectionRoutine() = 0;
  // Constructs a new instance of the DNS resolver present routine. See
  // diagnostics/cros_healthd/routines/network/dns_resolver_present.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine>
  MakeDnsResolverPresentRoutine() = 0;
  // Constructs a new instance of the DNS latency routine. See
  // diagnostics/cros_healthd/routines/network/dns_latency.h for details on the
  // routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeDnsLatencyRoutine() = 0;
  // Constructs a new instance of the DNS resolution routine. See
  // diagnostics/cros_healthd/routines/network/dns_resolution.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeDnsResolutionRoutine() = 0;
  // Constructs a new instance of the captive portal routine. See
  // diagnostics/cros_healthd/routines/network/captive_portal.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeCaptivePortalRoutine() = 0;
  // Constructs a new instance of the HTTP firewall routine. See
  // diagnostics/cros_healthd/routines/network/http_firewall.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeHttpFirewallRoutine() = 0;
  // Constructs a new instance of the HTTPS firewall routine. See
  // diagnostics/cros_healthd/routines/network/https_firewall.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeHttpsFirewallRoutine() = 0;
  // Constructs a new instance of the HTTPS latency routine. See
  // diagnostics/cros_healthd/routines/network/https_latency.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeHttpsLatencyRoutine() = 0;
  // Constructs a new instance of the video conferencing routine. See
  // diagnostics/cros_healthd/routines/network/video_conferencing.h for details
  // on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeVideoConferencingRoutine(
      const std::optional<std::string>& stun_server_hostname) = 0;
  // Constructs a new instance of the ARC HTTP routine. See
  // diagnostics/cros_healthd/routines/android_network/arc_http.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeArcHttpRoutine() = 0;
  // Constructs a new instance of the ARC Ping routine. See
  // diagnostics/cros_healthd/routines/android_network/arc_ping.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeArcPingRoutine() = 0;
  // Constructs a new instance of the ARC DNS Resolution routine. See
  // diagnostics/cros_healthd/routines/android_network/arc_dns_resolution.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeArcDnsResolutionRoutine() = 0;
  // Constructs a new instance of the sensor routine. See
  // diagnostics/cros_healthd/routines/sensor/sensitive_sensor.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeSensitiveSensorRoutine() = 0;
  // Constructs a new instance of the fingerprint routine. See
  // diagnostics/cros_healthd/routines/fingerprint/fingerprint.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeFingerprintRoutine() = 0;
  // Constructs a new instance of the fingerprint alive routine. See
  // diagnostics/cros_healthd/routines/fingerprint/fingerprint_alive.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeFingerprintAliveRoutine() = 0;
  // Constructs a new instance of the privacy screen routine. See
  // diagnostics/cros_healthd/routines/privacy_screen/privacy_screen.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakePrivacyScreenRoutine(
      bool target_state) = 0;
  // Constructs a new instance of the eMMC lifetime routine. See
  // diagnostics/cros_healthd/routines/storage/emmc_lifetime.h for details on
  // the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeEmmcLifetimeRoutine(
      org::chromium::debugdProxyInterface* debugd_proxy) = 0;
  // Constructs a new instance of the Bluetooth power routine. See
  // diagnostics/cros_healthd/routines/bluetooth/bluetooth_power.h for details
  // on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBluetoothPowerRoutine() = 0;
  // Constructs a new instance of the Bluetooth discovery routine. See
  // diagnostics/cros_healthd/routines/bluetooth/bluetooth_discovery.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine>
  MakeBluetoothDiscoveryRoutine() = 0;
  // Constructs a new instance of the Bluetooth scanning routine. See
  // diagnostics/cros_healthd/routines/bluetooth/bluetooth_scanning.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBluetoothScanningRoutine(
      const std::optional<base::TimeDelta>& exec_duration) = 0;
  // Constructs a new instance of the Bluetooth pairing routine. See
  // diagnostics/cros_healthd/routines/bluetooth/bluetooth_pairing.h for details
  // on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBluetoothPairingRoutine(
      const std::string& peripheral_id) = 0;
  // Constructs a new instance of the power button routine. See
  // diagnostics/cros_healthd/routines/hardware_button/power_button.h for
  // details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakePowerButtonRoutine(
      uint32_t timeout_seconds) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_
