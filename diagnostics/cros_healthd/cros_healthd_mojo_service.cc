// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <sys/types.h>

#include <utility>

#include <base/logging.h>

#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_health_ipc = ::chromeos::network_health::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    Context* context,
    FetchAggregator* fetch_aggregator,
    BluetoothEvents* bluetooth_events,
    LidEvents* lid_events,
    PowerEvents* power_events)
    : context_(context),
      fetch_aggregator_(fetch_aggregator),
      bluetooth_events_(bluetooth_events),
      lid_events_(lid_events),
      power_events_(power_events) {
  DCHECK(context_);
  DCHECK(fetch_aggregator_);
  DCHECK(bluetooth_events_);
  DCHECK(lid_events_);
  DCHECK(power_events_);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::AddBluetoothObserver(
    chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer) {
  bluetooth_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddLidObserver(
    chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer) {
  lid_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddPowerObserver(
    chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer) {
  power_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddNetworkObserver(
    mojo::PendingRemote<network_health_ipc::NetworkEventsObserver> observer) {
  context_->network_health_adapter()->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::ProbeProcessInfo(
    uint32_t process_id, ProbeProcessInfoCallback callback) {
  ProcessFetcher(context_, static_cast<pid_t>(process_id))
      .FetchProcessInfo(std::move(callback));
}

void CrosHealthdMojoService::ProbeTelemetryInfo(
    const std::vector<ProbeCategoryEnum>& categories,
    ProbeTelemetryInfoCallback callback) {
  return fetch_aggregator_->Run(categories, std::move(callback));
}

void CrosHealthdMojoService::GetServiceStatus(
    GetServiceStatusCallback callback) {
  auto response = chromeos::cros_healthd::mojom::ServiceStatus::New();
  response->network_health_bound =
      context_->network_health_adapter()->ServiceRemoteBound();
  response->network_diagnostics_bound =
      context_->network_diagnostics_adapter()->ServiceRemoteBound();
  std::move(callback).Run(std::move(response));
}

void CrosHealthdMojoService::AddProbeBinding(
    chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest request) {
  probe_binding_set_.AddBinding(this /* impl */, std::move(request));
}

void CrosHealthdMojoService::AddEventBinding(
    chromeos::cros_healthd::mojom::CrosHealthdEventServiceRequest request) {
  event_binding_set_.AddBinding(this /* impl */, std::move(request));
}

void CrosHealthdMojoService::AddSystemBinding(
    chromeos::cros_healthd::mojom::CrosHealthdSystemServiceRequest request) {
  system_binding_set_.AddBinding(this /* impl */, std::move(request));
}

}  // namespace diagnostics
