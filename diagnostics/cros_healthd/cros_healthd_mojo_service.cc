// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <sys/types.h>

#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <chromeos/mojo/service_constants.h>

#include "base/notreached.h"
#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;
namespace network_health_mojom = ::chromeos::network_health::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    Context* context,
    FetchAggregator* fetch_aggregator,
    EventAggregator* event_aggregator,
    BluetoothEvents* bluetooth_events,
    PowerEvents* power_events,
    AudioEvents* audio_events)
    : probe_provider_(this),
      event_provider_(this),
      system_provider_(this),
      routine_provider_(this),
      context_(context),
      fetch_aggregator_(fetch_aggregator),
      event_aggregator_(event_aggregator),
      bluetooth_events_(bluetooth_events),
      power_events_(power_events),
      audio_events_(audio_events) {
  DCHECK(context_);
  DCHECK(fetch_aggregator_);
  DCHECK(event_aggregator_);
  DCHECK(bluetooth_events_);
  DCHECK(power_events_);
  DCHECK(audio_events_);
  probe_provider_.Register(context->mojo_service()->GetServiceManager(),
                           chromeos::mojo_services::kCrosHealthdProbe);
  event_provider_.Register(context->mojo_service()->GetServiceManager(),
                           chromeos::mojo_services::kCrosHealthdEvent);
  system_provider_.Register(context->mojo_service()->GetServiceManager(),
                            chromeos::mojo_services::kCrosHealthdSystem);
  routine_provider_.Register(context->mojo_service()->GetServiceManager(),
                             chromeos::mojo_services::kCrosHealthdRoutines);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::AddBluetoothObserver(
    mojo::PendingRemote<mojom::CrosHealthdBluetoothObserver> observer) {
  bluetooth_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddLidObserver(
    mojo::PendingRemote<mojom::CrosHealthdLidObserver> observer) {
  LOG(FATAL) << "Deprecated cros healthd lid event API";
}

void CrosHealthdMojoService::AddPowerObserver(
    mojo::PendingRemote<mojom::CrosHealthdPowerObserver> observer) {
  power_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddNetworkObserver(
    mojo::PendingRemote<network_health_mojom::NetworkEventsObserver> observer) {
  context_->network_health_adapter()->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddAudioObserver(
    mojo::PendingRemote<mojom::CrosHealthdAudioObserver> observer) {
  audio_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddThunderboltObserver(
    mojo::PendingRemote<mojom::CrosHealthdThunderboltObserver> observer) {
  event_aggregator_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddUsbObserver(
    mojo::PendingRemote<mojom::CrosHealthdUsbObserver> observer) {
  event_aggregator_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddEventObserver(
    mojom::EventCategoryEnum category,
    mojo::PendingRemote<mojom::EventObserver> observer) {
  event_aggregator_->AddObserver(category, std::move(observer));
}

void CrosHealthdMojoService::ProbeProcessInfo(
    uint32_t process_id, ProbeProcessInfoCallback callback) {
  ProcessFetcher(context_).FetchProcessInfo(static_cast<pid_t>(process_id),
                                            std::move(callback));
}

void CrosHealthdMojoService::ProbeTelemetryInfo(
    const std::vector<ProbeCategoryEnum>& categories,
    ProbeTelemetryInfoCallback callback) {
  return fetch_aggregator_->Run(categories, std::move(callback));
}

void CrosHealthdMojoService::ProbeMultipleProcessInfo(
    const std::optional<std::vector<uint32_t>>& process_ids,
    const bool ignore_single_process_info,
    ProbeMultipleProcessInfoCallback callback) {
  ProcessFetcher(context_).FetchMultipleProcessInfo(
      process_ids, ignore_single_process_info, std::move(callback));
}

void CrosHealthdMojoService::CreateRoutine(
    mojom::RoutineArgumentPtr routine_arg,
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver) {
  NOTIMPLEMENTED();
}

void CrosHealthdMojoService::GetServiceStatus(
    GetServiceStatusCallback callback) {
  auto response = mojom::ServiceStatus::New();
  response->network_health_bound =
      context_->network_health_adapter()->ServiceRemoteBound();
  response->network_diagnostics_bound =
      context_->network_diagnostics_adapter()->ServiceRemoteBound();
  std::move(callback).Run(std::move(response));
}

void CrosHealthdMojoService::AddProbeReceiver(
    mojo::PendingReceiver<mojom::CrosHealthdProbeService> receiver) {
  probe_receiver_set_.Add(this /* impl */, std::move(receiver));
}

void CrosHealthdMojoService::AddEventReceiver(
    mojo::PendingReceiver<mojom::CrosHealthdEventService> receiver) {
  event_receiver_set_.Add(this /* impl */, std::move(receiver));
}

void CrosHealthdMojoService::AddSystemReceiver(
    mojo::PendingReceiver<mojom::CrosHealthdSystemService> receiver) {
  system_receiver_set_.Add(this /* impl */, std::move(receiver));
}

}  // namespace diagnostics
