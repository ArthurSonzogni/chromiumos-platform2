// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <sys/types.h>

#include <utility>

#include <base/check.h>
#include <base/logging.h>

#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_health_ipc = ::chromeos::network_health::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    Context* context,
    FetchAggregator* fetch_aggregator,
    BluetoothEvents* bluetooth_events,
    LidEvents* lid_events,
    PowerEvents* power_events,
    AudioEvents* audio_events,
    UdevEvents* udev_events)
    : context_(context),
      fetch_aggregator_(fetch_aggregator),
      bluetooth_events_(bluetooth_events),
      lid_events_(lid_events),
      power_events_(power_events),
      audio_events_(audio_events),
      udev_events_(udev_events) {
  DCHECK(context_);
  DCHECK(fetch_aggregator_);
  DCHECK(bluetooth_events_);
  DCHECK(lid_events_);
  DCHECK(power_events_);
  DCHECK(audio_events_);
  DCHECK(udev_events_);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::AddBluetoothObserver(
    mojo::PendingRemote<
        chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver> observer) {
  bluetooth_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddLidObserver(
    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdLidObserver>
        observer) {
  lid_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddPowerObserver(
    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdPowerObserver>
        observer) {
  power_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddNetworkObserver(
    mojo::PendingRemote<network_health_ipc::NetworkEventsObserver> observer) {
  context_->network_health_adapter()->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddAudioObserver(
    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdAudioObserver>
        observer) {
  audio_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddThunderboltObserver(
    mojo::PendingRemote<
        chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver>
        observer) {
  udev_events_->AddThunderboltObserver(std::move(observer));
}

void CrosHealthdMojoService::AddUsbObserver(
    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdUsbObserver>
        observer) {
  udev_events_->AddUsbObserver(std::move(observer));
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

void CrosHealthdMojoService::AddProbeReceiver(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdProbeService> receiver) {
  probe_receiver_set_.Add(this /* impl */, std::move(receiver));
}

void CrosHealthdMojoService::AddEventReceiver(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdEventService> receiver) {
  event_receiver_set_.Add(this /* impl */, std::move(receiver));
}

void CrosHealthdMojoService::AddSystemReceiver(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdSystemService> receiver) {
  system_receiver_set_.Add(this /* impl */, std::move(receiver));
}

}  // namespace diagnostics
