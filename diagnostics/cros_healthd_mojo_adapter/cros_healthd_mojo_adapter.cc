// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/run_loop.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate_impl.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

// Provides a mojo connection to cros_healthd. See mojo/cros_healthd.mojom for
// details on cros_healthd's mojo interface. This should only be used by
// processes whose only mojo connection is to cros_healthd.
class CrosHealthdMojoAdapterImpl final : public CrosHealthdMojoAdapter {
 public:
  // Override |delegate| for testing only.
  explicit CrosHealthdMojoAdapterImpl(
      CrosHealthdMojoAdapterDelegate* delegate = nullptr);
  CrosHealthdMojoAdapterImpl(const CrosHealthdMojoAdapterImpl&) = delete;
  CrosHealthdMojoAdapterImpl& operator=(const CrosHealthdMojoAdapterImpl&) =
      delete;
  ~CrosHealthdMojoAdapterImpl() override;

  // Gets cros_healthd service status.
  ash::cros_healthd::mojom::ServiceStatusPtr GetServiceStatus() override;

  // Gets telemetry information from cros_healthd.
  ash::cros_healthd::mojom::TelemetryInfoPtr GetTelemetryInfo(
      const std::vector<ash::cros_healthd::mojom::ProbeCategoryEnum>&
          categories_to_probe) override;

  // Gets information about a specific process on the device from cros_healthd.
  ash::cros_healthd::mojom::ProcessResultPtr GetProcessInfo(pid_t pid) override;

  // Gets information about multiple/ all processes on the device from
  // cros_healthd.
  ash::cros_healthd::mojom::MultipleProcessResultPtr GetMultipleProcessInfo(
      const std::optional<std::vector<uint32_t>>& pids,
      const bool ignore_single_process_info) override;

  // Runs the urandom routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunUrandomRoutine(
      const std::optional<base::TimeDelta>& length_seconds) override;

  // Runs the battery capacity routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunBatteryCapacityRoutine()
      override;

  // Runs the battery health routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunBatteryHealthRoutine()
      override;

  // Runs the smartctl-check routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunSmartctlCheckRoutine()
      override;

  // Runs the AC power routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunAcPowerRoutine(
      ash::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const std::optional<std::string>& expected_power_type) override;

  // Runs the CPU cache routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunCpuCacheRoutine(
      const std::optional<base::TimeDelta>& exec_duration) override;

  // Runs the CPU stress routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunCpuStressRoutine(
      const std::optional<base::TimeDelta>& exec_duration) override;

  // Runs the floating-point-accuracy routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunFloatingPointAccuracyRoutine(
      const std::optional<base::TimeDelta>& exec_duration) override;

  // Runs the NvmeWearLevel routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunNvmeWearLevelRoutine(
      const std::optional<uint32_t>& wear_level_threshold) override;

  // Runs the NvmeSelfTest routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunNvmeSelfTestRoutine(
      ash::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type)
      override;

  // Runs the disk read routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunDiskReadRoutine(
      ash::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb) override;

  // Runs the prime search routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunPrimeSearchRoutine(
      const std::optional<base::TimeDelta>& exec_duration) override;

  // Runs the battery discharge routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed) override;

  // Runs the battery charge routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunBatteryChargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t minimum_charge_percent_required) override;

  // Runs the LAN connectivity routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunLanConnectivityRoutine()
      override;

  // Runs the signal strength routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunSignalStrengthRoutine()
      override;

  // Runs the memory routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunMemoryRoutine() override;

  // Runs the gateway can be pinged routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunGatewayCanBePingedRoutine()
      override;

  // Runs the has secure WiFi connection routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr
  RunHasSecureWiFiConnectionRoutine() override;

  // Runs the DNS resolver present routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunDnsResolverPresentRoutine()
      override;

  // Runs the DNS latency routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunDnsLatencyRoutine()
      override;

  // Runs the DNS resolution routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunDnsResolutionRoutine()
      override;

  // Runs the captive portal routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunCaptivePortalRoutine()
      override;

  // Runs the HTTP firewall routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunHttpFirewallRoutine()
      override;

  // Runs the HTTPS firewall routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunHttpsFirewallRoutine()
      override;

  // Runs the HTTPS latency routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunHttpsLatencyRoutine()
      override;

  // Runs the video conferencing routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunVideoConferencingRoutine(
      const std::optional<std::string>& stun_server_hostname) override;

  // Runs the ARC HTTP routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunArcHttpRoutine() override;

  // Runs the ARC Ping routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunArcPingRoutine() override;

  // Runs the ARC DNS resolution routine.
  ash::cros_healthd::mojom::RunRoutineResponsePtr RunArcDnsResolutionRoutine()
      override;

  // Returns which routines are available on the platform.
  std::optional<std::vector<ash::cros_healthd::mojom::DiagnosticRoutineEnum>>
  GetAvailableRoutines() override;

  // Gets an update for the specified routine.
  ash::cros_healthd::mojom::RoutineUpdatePtr GetRoutineUpdate(
      int32_t id,
      ash::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output) override;

  // Subscribes the client to Bluetooth events.
  bool AddBluetoothObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdBluetoothObserver> observer)
      override;

  // Subscribes the client to lid events.
  bool AddLidObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdLidObserver>
          observer) override;

  // Subscribes the client to power events.
  bool AddPowerObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdPowerObserver>
          observer) override;

  // Subscribes the client to network events.
  bool AddNetworkObserver(
      mojo::PendingRemote<
          chromeos::network_health::mojom::NetworkEventsObserver> observer)
      override;

  // Subscribes the client to audio events.
  bool AddAudioObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdAudioObserver>
          observer) override;

  // Subscribes the client to Thunderbolt events.
  bool AddThunderboltObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdThunderboltObserver> observer)
      override;

  // Subscribes the client to USB events.
  bool AddUsbObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdUsbObserver>
          observer) override;

 private:
  // Establishes a mojo connection with cros_healthd.
  bool Connect();

  // Default delegate implementation.
  std::unique_ptr<CrosHealthdMojoAdapterDelegateImpl> delegate_impl_;
  // Unowned. Must outlive this instance.
  CrosHealthdMojoAdapterDelegate* delegate_;

  // Binds to an implementation of CrosHealthdServiceFactory. The implementation
  // is provided by cros_healthd. Allows calling cros_healthd's mojo factory
  // methods.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdServiceFactory>
      cros_healthd_service_factory_;
  // Binds to an implementation of CrosHealthdProbeService. The implementation
  // is provided by cros_healthd. Allows calling cros_healthd's probe-related
  // mojo methods.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdProbeService>
      cros_healthd_probe_service_;
  // Binds to an implementation of CrosHealthdDiagnosticsService. The
  // implementation is provided by cros_healthd. Allows calling cros_healthd's
  // diagnostics-related mojo methods.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdDiagnosticsService>
      cros_healthd_diagnostics_service_;
  // Binds to an implementation of CrosHealthdEventService. The
  // implementation is provided by cros_healthd. Allows calling cros_healthd's
  // event-related mojo methods.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdEventService>
      cros_healthd_event_service_;
  // Binds to an implementation of CrosHealthdSystemService. The
  // implementation is provided by cros_healthd. Allows calling cros_healthd's
  // system-related mojo methods.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdSystemService>
      cros_healthd_system_service_;
};

// Saves |response| to |response_destination|.
template <class T>
void OnMojoResponseReceived(T* response_destination,
                            base::OnceClosure quit_closure,
                            T response) {
  *response_destination = std::move(response);
  std::move(quit_closure).Run();
}

CrosHealthdMojoAdapterImpl::CrosHealthdMojoAdapterImpl(
    CrosHealthdMojoAdapterDelegate* delegate) {
  if (delegate) {
    delegate_ = delegate;
  } else {
    delegate_impl_ = std::make_unique<CrosHealthdMojoAdapterDelegateImpl>();
    delegate_ = delegate_impl_.get();
  }
  DCHECK(delegate_);
}

CrosHealthdMojoAdapterImpl::~CrosHealthdMojoAdapterImpl() = default;

// Gets cros_healthd service status.
ash::cros_healthd::mojom::ServiceStatusPtr
CrosHealthdMojoAdapterImpl::GetServiceStatus() {
  if (!cros_healthd_system_service_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::ServiceStatusPtr response;
  base::RunLoop run_loop;
  cros_healthd_system_service_->GetServiceStatus(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::ServiceStatusPtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::TelemetryInfoPtr
CrosHealthdMojoAdapterImpl::GetTelemetryInfo(
    const std::vector<ash::cros_healthd::mojom::ProbeCategoryEnum>&
        categories_to_probe) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::TelemetryInfoPtr response;
  base::RunLoop run_loop;
  cros_healthd_probe_service_->ProbeTelemetryInfo(
      categories_to_probe,
      base::BindOnce(
          &OnMojoResponseReceived<ash::cros_healthd::mojom::TelemetryInfoPtr>,
          &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::ProcessResultPtr
CrosHealthdMojoAdapterImpl::GetProcessInfo(pid_t pid) {
  if (!cros_healthd_probe_service_.is_bound())
    Connect();

  ash::cros_healthd::mojom::ProcessResultPtr response;
  base::RunLoop run_loop;
  cros_healthd_probe_service_->ProbeProcessInfo(
      pid,
      base::BindOnce(
          &OnMojoResponseReceived<ash::cros_healthd::mojom::ProcessResultPtr>,
          &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::MultipleProcessResultPtr
CrosHealthdMojoAdapterImpl::GetMultipleProcessInfo(
    const std::optional<std::vector<uint32_t>>& pids,
    const bool ignore_single_process_info) {
  if (!cros_healthd_probe_service_.is_bound())
    Connect();

  ash::cros_healthd::mojom::MultipleProcessResultPtr response;
  base::RunLoop run_loop;
  cros_healthd_probe_service_->ProbeMultipleProcessInfo(
      pids, ignore_single_process_info,
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::MultipleProcessResultPtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunUrandomRoutine(
    const std::optional<base::TimeDelta>& length_seconds) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  ash::cros_healthd::mojom::NullableUint32Ptr length_seconds_parameter;
  if (length_seconds.has_value()) {
    length_seconds_parameter = ash::cros_healthd::mojom::NullableUint32::New(
        length_seconds.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunUrandomRoutine(
      std::move(length_seconds_parameter),
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunBatteryCapacityRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryCapacityRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunBatteryHealthRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryHealthRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunSmartctlCheckRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunSmartctlCheckRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunAcPowerRoutine(
    ash::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const std::optional<std::string>& expected_power_type) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunAcPowerRoutine(
      expected_status, expected_power_type,
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunCpuCacheRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  ash::cros_healthd::mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter = ash::cros_healthd::mojom::NullableUint32::New(
        exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunCpuCacheRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunCpuStressRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  ash::cros_healthd::mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter = ash::cros_healthd::mojom::NullableUint32::New(
        exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunCpuStressRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunFloatingPointAccuracyRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  ash::cros_healthd::mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter = ash::cros_healthd::mojom::NullableUint32::New(
        exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunFloatingPointAccuracyRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunNvmeWearLevelRoutine(
    const std::optional<uint32_t>& wear_level_threshold) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  ash::cros_healthd::mojom::NullableUint32Ptr wear_level_threshold_parameter;
  if (wear_level_threshold.has_value()) {
    wear_level_threshold_parameter =
        ash::cros_healthd::mojom::NullableUint32::New(
            wear_level_threshold.value());
  }
  cros_healthd_diagnostics_service_->RunNvmeWearLevelRoutine(
      std::move(wear_level_threshold_parameter),
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunNvmeSelfTestRoutine(
    ash::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunNvmeSelfTestRoutine(
      nvme_self_test_type,
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunDiskReadRoutine(
    ash::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDiskReadRoutine(
      type, exec_duration.InSeconds(), file_size_mb,
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunPrimeSearchRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  ash::cros_healthd::mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter = ash::cros_healthd::mojom::NullableUint32::New(
        exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunPrimeSearchRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryDischargeRoutine(
      exec_duration.InSeconds(), maximum_discharge_percent_allowed,
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunBatteryChargeRoutine(
    base::TimeDelta exec_duration, uint32_t minimum_charge_percent_required) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryChargeRoutine(
      exec_duration.InSeconds(), minimum_charge_percent_required,
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunLanConnectivityRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunLanConnectivityRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunSignalStrengthRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunSignalStrengthRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunMemoryRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunMemoryRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunGatewayCanBePingedRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunGatewayCanBePingedRoutine(
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunHasSecureWiFiConnectionRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHasSecureWiFiConnectionRoutine(
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunDnsResolverPresentRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDnsResolverPresentRoutine(
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunDnsLatencyRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDnsLatencyRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunDnsResolutionRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDnsResolutionRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunCaptivePortalRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunCaptivePortalRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunHttpFirewallRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHttpFirewallRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunHttpsFirewallRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHttpsFirewallRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunHttpsLatencyRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHttpsLatencyRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunVideoConferencingRoutine(
      stun_server_hostname,
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunArcHttpRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunArcHttpRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunArcPingRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunArcPingRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunArcDnsResolutionRoutine() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunArcDnsResolutionRoutine(base::BindOnce(
      &OnMojoResponseReceived<ash::cros_healthd::mojom::RunRoutineResponsePtr>,
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

std::optional<std::vector<ash::cros_healthd::mojom::DiagnosticRoutineEnum>>
CrosHealthdMojoAdapterImpl::GetAvailableRoutines() {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return std::nullopt;

  std::vector<ash::cros_healthd::mojom::DiagnosticRoutineEnum> response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetAvailableRoutines(base::BindOnce(
      [](std::vector<ash::cros_healthd::mojom::DiagnosticRoutineEnum>* out,
         base::OnceClosure quit_closure,
         const std::vector<ash::cros_healthd::mojom::DiagnosticRoutineEnum>&
             routines) {
        *out = routines;
        std::move(quit_closure).Run();
      },
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

ash::cros_healthd::mojom::RoutineUpdatePtr
CrosHealthdMojoAdapterImpl::GetRoutineUpdate(
    int32_t id,
    ash::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
    bool include_output) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return nullptr;

  ash::cros_healthd::mojom::RoutineUpdatePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetRoutineUpdate(
      id, command, include_output,
      base::BindOnce(
          &OnMojoResponseReceived<ash::cros_healthd::mojom::RoutineUpdatePtr>,
          &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

bool CrosHealthdMojoAdapterImpl::AddBluetoothObserver(
    mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdBluetoothObserver>
        observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddBluetoothObserver(std::move(observer));
  return true;
}

bool CrosHealthdMojoAdapterImpl::AddLidObserver(
    mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdLidObserver>
        observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddLidObserver(std::move(observer));
  return true;
}

bool CrosHealthdMojoAdapterImpl::AddPowerObserver(
    mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdPowerObserver>
        observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddPowerObserver(std::move(observer));
  return true;
}

bool CrosHealthdMojoAdapterImpl::AddNetworkObserver(
    mojo::PendingRemote<chromeos::network_health::mojom::NetworkEventsObserver>
        observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddNetworkObserver(std::move(observer));
  return true;
}

bool CrosHealthdMojoAdapterImpl::AddAudioObserver(
    mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdAudioObserver>
        observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddAudioObserver(std::move(observer));
  return true;
}

bool CrosHealthdMojoAdapterImpl::AddThunderboltObserver(
    mojo::PendingRemote<
        ash::cros_healthd::mojom::CrosHealthdThunderboltObserver> observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddThunderboltObserver(std::move(observer));
  return true;
}

bool CrosHealthdMojoAdapterImpl::AddUsbObserver(
    mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdUsbObserver>
        observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddUsbObserver(std::move(observer));
  return true;
}

bool CrosHealthdMojoAdapterImpl::Connect() {
  auto opt_pending_service_factory = delegate_->GetCrosHealthdServiceFactory();
  if (!opt_pending_service_factory)
    return false;

  cros_healthd_service_factory_.Bind(
      std::move(opt_pending_service_factory).value());

  // Bind the probe, diagnostics and event services.
  cros_healthd_service_factory_->GetProbeService(
      cros_healthd_probe_service_.BindNewPipeAndPassReceiver());
  cros_healthd_service_factory_->GetDiagnosticsService(
      cros_healthd_diagnostics_service_.BindNewPipeAndPassReceiver());
  cros_healthd_service_factory_->GetEventService(
      cros_healthd_event_service_.BindNewPipeAndPassReceiver());
  cros_healthd_service_factory_->GetSystemService(
      cros_healthd_system_service_.BindNewPipeAndPassReceiver());

  return true;
}

}  // namespace

std::unique_ptr<CrosHealthdMojoAdapter> CrosHealthdMojoAdapter::Create() {
  return std::make_unique<CrosHealthdMojoAdapterImpl>();
}

}  // namespace diagnostics
