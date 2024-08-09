// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <sys/types.h>

#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <chromeos/mojo/service_constants.h>

#include "diagnostics/cros_healthd/event_aggregator.h"
#include "diagnostics/cros_healthd/fetch_aggregator.h"
#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;
namespace network_health_mojom = ::chromeos::network_health::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    Context* context,
    FetchAggregator* fetch_aggregator,
    EventAggregator* event_aggregator)
    : RoutineService(context),
      context_(context),
      fetch_aggregator_(fetch_aggregator),
      event_aggregator_(event_aggregator) {
  CHECK(context_);
  CHECK(fetch_aggregator_);
  CHECK(event_aggregator_);
  probe_provider_.Register(context->mojo_service()->GetServiceManager(),
                           chromeos::mojo_services::kCrosHealthdProbe);
  event_provider_.Register(context->mojo_service()->GetServiceManager(),
                           chromeos::mojo_services::kCrosHealthdEvent);
  routine_provider_.Register(context->mojo_service()->GetServiceManager(),
                             chromeos::mojo_services::kCrosHealthdRoutines);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::DEPRECATED_AddBluetoothObserver(
    mojo::PendingRemote<mojom::CrosHealthdBluetoothObserver> observer) {
  LOG(FATAL) << "Deprecated cros healthd lid event API";
}

void CrosHealthdMojoService::DEPRECATED_AddLidObserver(
    mojo::PendingRemote<mojom::CrosHealthdLidObserver> observer) {
  LOG(FATAL) << "Deprecated cros healthd lid event API";
}

void CrosHealthdMojoService::DEPRECATED_AddPowerObserver(
    mojo::PendingRemote<mojom::CrosHealthdPowerObserver> observer) {
  event_aggregator_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddNetworkObserver(
    mojo::PendingRemote<network_health_mojom::NetworkEventsObserver> observer) {
  auto* network_health = context_->mojo_service()->GetNetworkHealth();
  if (!network_health) {
    LOG(ERROR) << "Network health service is unavailable.";
    return;
  }
  network_health->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::DEPRECATED_AddAudioObserver(
    mojo::PendingRemote<mojom::CrosHealthdAudioObserver> observer) {
  LOG(WARNING) << "Deprecated cros healthd audio event API";
}

void CrosHealthdMojoService::DEPRECATED_AddThunderboltObserver(
    mojo::PendingRemote<mojom::CrosHealthdThunderboltObserver> observer) {
  event_aggregator_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::DEPRECATED_AddUsbObserver(
    mojo::PendingRemote<mojom::CrosHealthdUsbObserver> observer) {
  event_aggregator_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddEventObserver(
    mojom::EventCategoryEnum category,
    mojo::PendingRemote<mojom::EventObserver> observer) {
  event_aggregator_->AddObserver(category, std::move(observer));
}

void CrosHealthdMojoService::IsEventSupported(
    mojom::EventCategoryEnum category, IsEventSupportedCallback callback) {
  event_aggregator_->IsEventSupported(category, std::move(callback));
}

void CrosHealthdMojoService::ProbeProcessInfo(
    uint32_t process_id, ProbeProcessInfoCallback callback) {
  FetchProcessInfo(context_, process_id, std::move(callback));
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
  FetchMultipleProcessInfo(context_, process_ids, ignore_single_process_info,
                           std::move(callback));
}

}  // namespace diagnostics
