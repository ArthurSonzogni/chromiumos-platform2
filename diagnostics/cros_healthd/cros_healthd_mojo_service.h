// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_

#include <cstdint>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo_service_manager/lib/simple_mojo_service_provider.h>

#include "diagnostics/cros_healthd/routines/routine_service.h"
#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"

namespace diagnostics {
class Context;
class EventAggregator;
class FetchAggregator;

// Implements the Mojo interfaces exposed by the cros_healthd daemon:
// - CrosHealthdProbeService
// - CrosHealthdEventService
// - CrosHealthdRoutinesService
//
// The Mojo interface `CrosHealthdDiagnosticsService` is implemented by the
// class `CrosHealthdDiagnosticsService`.
class CrosHealthdMojoService final
    : public ash::cros_healthd::mojom::CrosHealthdEventService,
      public ash::cros_healthd::mojom::CrosHealthdProbeService,
      public RoutineService {
 public:
  using ProbeCategoryEnum = ::ash::cros_healthd::mojom::ProbeCategoryEnum;

  // |fetch_aggregator| - responsible for fulfilling probe requests.
  // |event_aggregator| - responsible for fulfilling event requests.
  CrosHealthdMojoService(Context* context,
                         FetchAggregator* fetch_aggregator,
                         EventAggregator* event_aggregator);
  CrosHealthdMojoService(const CrosHealthdMojoService&) = delete;
  CrosHealthdMojoService& operator=(const CrosHealthdMojoService&) = delete;
  ~CrosHealthdMojoService() override;

  // ash::cros_healthd::mojom::CrosHealthdEventService overrides:
  void DEPRECATED_AddBluetoothObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdBluetoothObserver> observer)
      override;
  void DEPRECATED_AddLidObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdLidObserver>
          observer) override;
  void DEPRECATED_AddPowerObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdPowerObserver>
          observer) override;
  void AddNetworkObserver(
      mojo::PendingRemote<
          chromeos::network_health::mojom::NetworkEventsObserver> observer)
      override;
  void DEPRECATED_AddAudioObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdAudioObserver>
          observer) override;
  void DEPRECATED_AddThunderboltObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdThunderboltObserver> observer)
      override;
  void DEPRECATED_AddUsbObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdUsbObserver>
          observer) override;
  void AddEventObserver(
      ash::cros_healthd::mojom::EventCategoryEnum category,
      mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer)
      override;
  void IsEventSupported(ash::cros_healthd::mojom::EventCategoryEnum category,
                        IsEventSupportedCallback callback) override;

  // ash::cros_healthd::mojom::CrosHealthdProbeService overrides:
  void ProbeProcessInfo(uint32_t process_id,
                        ProbeProcessInfoCallback callback) override;
  void ProbeTelemetryInfo(const std::vector<ProbeCategoryEnum>& categories,
                          ProbeTelemetryInfoCallback callback) override;
  void ProbeMultipleProcessInfo(
      const std::optional<std::vector<uint32_t>>& process_ids,
      bool ignore_single_process_info,
      ProbeMultipleProcessInfoCallback callback) override;

 private:
  // Mojo service providers to provide services to mojo service manager.
  chromeos::mojo_service_manager::SimpleMojoServiceProvider<
      ash::cros_healthd::mojom::CrosHealthdProbeService>
      probe_provider_{this};
  chromeos::mojo_service_manager::SimpleMojoServiceProvider<
      ash::cros_healthd::mojom::CrosHealthdEventService>
      event_provider_{this};
  chromeos::mojo_service_manager::SimpleMojoServiceProvider<
      ash::cros_healthd::mojom::CrosHealthdRoutinesService>
      routine_provider_{this};

  // Unowned. The following instances should outlive this instance.
  Context* const context_ = nullptr;
  FetchAggregator* const fetch_aggregator_ = nullptr;
  EventAggregator* const event_aggregator_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
