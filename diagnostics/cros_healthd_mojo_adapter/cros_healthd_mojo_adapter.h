// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/time/time.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Provides a mojo connection to cros_healthd. See mojo/cros_healthd.mojom for
// details on cros_healthd's mojo interface. The interface is used synchronous
// signature and handled non nullable primitives in Mojo for caller convenience.
//
// This should only be used by processes whose only mojo connection is to
// cros_healthd. This is a public interface of the class providing the
// functionality.
class CrosHealthdMojoAdapter {
 public:
  virtual ~CrosHealthdMojoAdapter() {}

  // Creates an instance of CrosHealthdMojoAdapter.
  static std::unique_ptr<CrosHealthdMojoAdapter> Create();

  // Gets cros_healthd service status.
  virtual ash::cros_healthd::mojom::ServiceStatusPtr GetServiceStatus() = 0;

  // Gets telemetry information from cros_healthd.
  virtual ash::cros_healthd::mojom::TelemetryInfoPtr GetTelemetryInfo(
      const std::vector<ash::cros_healthd::mojom::ProbeCategoryEnum>&
          categories_to_probe) = 0;

  // Gets information about a specific process from cros_healthd.
  virtual ash::cros_healthd::mojom::ProcessResultPtr GetProcessInfo(
      pid_t pid) = 0;

  // Gets information about multiple/ all processes from cros_healthd.
  virtual ash::cros_healthd::mojom::MultipleProcessResultPtr
  GetMultipleProcessInfo(const std::optional<std::vector<uint32_t>>& pids,
                         const bool skip_single_process_info) = 0;

  // Runs the urandom routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr RunUrandomRoutine(
      const std::optional<base::TimeDelta>& length_seconds) = 0;

  // Runs the battery capacity routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryCapacityRoutine() = 0;

  // Runs the battery health routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryHealthRoutine() = 0;

  // Runs the smartctl-check routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunSmartctlCheckRoutine() = 0;

  // Runs the AC power routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr RunAcPowerRoutine(
      ash::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const std::optional<std::string>& expected_power_type) = 0;

  // Runs the CPU cache routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr RunCpuCacheRoutine(
      const std::optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the CPU stress routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr RunCpuStressRoutine(
      const std::optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the floating-point-accuracy routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunFloatingPointAccuracyRoutine(
      const std::optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the NvmeWearLevel routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunNvmeWearLevelRoutine(
      const std::optional<uint32_t>& wear_level_threshold) = 0;

  // Runs the NvmeSelfTest routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunNvmeSelfTestRoutine(
      ash::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) = 0;

  // Runs the disk read routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr RunDiskReadRoutine(
      ash::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb) = 0;

  // Runs the prime search routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr RunPrimeSearchRoutine(
      const std::optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the battery discharge routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryDischargeRoutine(base::TimeDelta exec_duration,
                             uint32_t maximum_discharge_percent_allowed) = 0;

  // Runs the battery charge routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryChargeRoutine(base::TimeDelta exec_duration,
                          uint32_t minimum_charge_percent_required) = 0;

  // Runs the LAN connectivity routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunLanConnectivityRoutine() = 0;

  // Runs the signal strength routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunSignalStrengthRoutine() = 0;

  // Runs the memory routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunMemoryRoutine() = 0;

  // Runs the gateway can be pinged routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunGatewayCanBePingedRoutine() = 0;

  // Runs the has secure WiFi connection routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunHasSecureWiFiConnectionRoutine() = 0;

  // Runs the DNS resolver present routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunDnsResolverPresentRoutine() = 0;

  // Runs the DNS latency routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunDnsLatencyRoutine() = 0;

  // Runs the DNS resolution routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunDnsResolutionRoutine() = 0;

  // Runs the captive portal routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunCaptivePortalRoutine() = 0;

  // Runs the HTTP firewall routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunHttpFirewallRoutine() = 0;

  // Runs the HTTPS firewall routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunHttpsFirewallRoutine() = 0;

  // Runs the HTTPS latency routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunHttpsLatencyRoutine() = 0;

  // Runs the video conferencing routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunVideoConferencingRoutine(
      const std::optional<std::string>& stun_server_hostname) = 0;

  // Runs the ARC HTTP routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunArcHttpRoutine() = 0;

  // Runs the ARC Ping routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunArcPingRoutine() = 0;

  // Runs the ARC DNS resolution routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunArcDnsResolutionRoutine() = 0;

  // Runs the sensitive sensor routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunSensitiveSensorRoutine() = 0;

  // Runs the fingerprint routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunFingerprintRoutine() = 0;

  // Runs the fingerprint alive routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunFingerprintAliveRoutine() = 0;

  // Runs the privacy screen routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunPrivacyScreenRoutine(bool target_state) = 0;

  // Runs the LED lit up routine.
  virtual ash::cros_healthd::mojom::RunRoutineResponsePtr RunLedLitUpRoutine(
      ash::cros_healthd::mojom::LedName name,
      ash::cros_healthd::mojom::LedColor color,
      mojo::PendingRemote<ash::cros_healthd::mojom::LedLitUpRoutineReplier>
          replier) = 0;

  // Returns which routines are available on the platform.
  virtual std::optional<
      std::vector<ash::cros_healthd::mojom::DiagnosticRoutineEnum>>
  GetAvailableRoutines() = 0;

  // Gets an update for the specified routine.
  virtual ash::cros_healthd::mojom::RoutineUpdatePtr GetRoutineUpdate(
      int32_t id,
      ash::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output) = 0;

  // Subscribes the client to Bluetooth events.
  virtual bool AddBluetoothObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdBluetoothObserver> observer) = 0;

  // Subscribes the client to lid events.
  virtual bool AddLidObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdLidObserver>
          observer) = 0;

  // Subscribes the client to power events.
  virtual bool AddPowerObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdPowerObserver>
          observer) = 0;

  // Subscribes the client to network events.
  virtual bool AddNetworkObserver(
      mojo::PendingRemote<
          chromeos::network_health::mojom::NetworkEventsObserver> observer) = 0;

  // Subscribes the client to audio events.
  virtual bool AddAudioObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdAudioObserver>
          observer) = 0;

  // Subscribes the client to Thunderbolt events.
  virtual bool AddThunderboltObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdThunderboltObserver>
          observer) = 0;

  // Subscribes the client to USB events.
  virtual bool AddUsbObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdUsbObserver>
          observer) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
