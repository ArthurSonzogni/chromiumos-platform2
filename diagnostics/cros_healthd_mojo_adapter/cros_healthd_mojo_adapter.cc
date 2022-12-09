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

  // Subscribes the client to events according to |category|.
  bool AddEventObserver(
      ash::cros_healthd::mojom::EventCategoryEnum category,
      mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer)
      override;

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

bool CrosHealthdMojoAdapterImpl::AddEventObserver(
    ash::cros_healthd::mojom::EventCategoryEnum category,
    mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer) {
  if (!cros_healthd_service_factory_.is_bound() && !Connect())
    return false;

  cros_healthd_event_service_->AddEventObserver(category, std::move(observer));
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
