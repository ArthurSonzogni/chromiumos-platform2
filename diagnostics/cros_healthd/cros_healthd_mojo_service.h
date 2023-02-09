// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_

#include <cstdint>
#include <vector>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "diagnostics/cros_healthd/event_aggregator.h"
#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/fetch_aggregator.h"
#include "diagnostics/cros_healthd/utils/mojo_service_provider.h"
#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

// Implements the "CrosHealthdService" Mojo interface exposed by the
// cros_healthd daemon (see the API definition at mojo/cros_healthd.mojom)
class CrosHealthdMojoService final
    : public ash::cros_healthd::mojom::CrosHealthdEventService,
      public ash::cros_healthd::mojom::CrosHealthdProbeService,
      public ash::cros_healthd::mojom::CrosHealthdSystemService,
      public ash::cros_healthd::mojom::CrosHealthdRoutinesService {
 public:
  using ProbeCategoryEnum = ::ash::cros_healthd::mojom::ProbeCategoryEnum;

  // |fetch_aggregator| - responsible for fulfilling probe requests.
  // |event_aggregator| - responsible for fulfilling event requests.
  // |bluetooth_events| - BluetoothEvents implementation.
  CrosHealthdMojoService(Context* context,
                         FetchAggregator* fetch_aggregator,
                         EventAggregator* event_aggregator,
                         BluetoothEvents* bluetooth_events);
  CrosHealthdMojoService(const CrosHealthdMojoService&) = delete;
  CrosHealthdMojoService& operator=(const CrosHealthdMojoService&) = delete;
  ~CrosHealthdMojoService() override;

  // ash::cros_healthd::mojom::CrosHealthdEventService overrides:
  void AddBluetoothObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdBluetoothObserver> observer)
      override;
  void AddLidObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdLidObserver>
          observer) override;
  void AddPowerObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdPowerObserver>
          observer) override;
  void AddNetworkObserver(
      mojo::PendingRemote<
          chromeos::network_health::mojom::NetworkEventsObserver> observer)
      override;
  void AddAudioObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdAudioObserver>
          observer) override;
  void AddThunderboltObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdThunderboltObserver> observer)
      override;
  void AddUsbObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdUsbObserver>
          observer) override;
  void AddEventObserver(
      ash::cros_healthd::mojom::EventCategoryEnum category,
      mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer)
      override;

  // ash::cros_healthd::mojom::CrosHealthdProbeService overrides:
  void ProbeProcessInfo(uint32_t process_id,
                        ProbeProcessInfoCallback callback) override;
  void ProbeTelemetryInfo(const std::vector<ProbeCategoryEnum>& categories,
                          ProbeTelemetryInfoCallback callback) override;
  void ProbeMultipleProcessInfo(
      const std::optional<std::vector<uint32_t>>& process_ids,
      bool ignore_single_process_info,
      ProbeMultipleProcessInfoCallback callback) override;

  // ash::cros_healthd::mojom::CrosHealthdSystemService overrides:
  void GetServiceStatus(GetServiceStatusCallback callback) override;

  // ash::cros_healthd::mojom::CrosHealthdRoutinesService overrides:
  void CreateRoutine(
      ash::cros_healthd::mojom::RoutineArgumentPtr routine_arg,
      mojo::PendingReceiver<ash::cros_healthd::mojom::RoutineControl>
          routine_receiver) override;

  // Adds a new binding to the internal binding sets.
  void AddProbeReceiver(
      mojo::PendingReceiver<ash::cros_healthd::mojom::CrosHealthdProbeService>
          receiver);
  void AddEventReceiver(
      mojo::PendingReceiver<ash::cros_healthd::mojom::CrosHealthdEventService>
          receiver);
  void AddSystemReceiver(
      mojo::PendingReceiver<ash::cros_healthd::mojom::CrosHealthdSystemService>
          receiver);

 private:
  // Mojo binding sets that connect |this| with message pipes, allowing the
  // remote ends to call our methods.
  mojo::ReceiverSet<ash::cros_healthd::mojom::CrosHealthdProbeService>
      probe_receiver_set_;
  mojo::ReceiverSet<ash::cros_healthd::mojom::CrosHealthdEventService>
      event_receiver_set_;
  mojo::ReceiverSet<ash::cros_healthd::mojom::CrosHealthdSystemService>
      system_receiver_set_;
  // Mojo service providers to provide services to mojo service manager.
  MojoServiceProvider<ash::cros_healthd::mojom::CrosHealthdProbeService>
      probe_provider_;
  MojoServiceProvider<ash::cros_healthd::mojom::CrosHealthdEventService>
      event_provider_;
  MojoServiceProvider<ash::cros_healthd::mojom::CrosHealthdSystemService>
      system_provider_;
  MojoServiceProvider<ash::cros_healthd::mojom::CrosHealthdRoutinesService>
      routine_provider_;

  // Unowned. The following instances should outlive this instance.
  Context* const context_ = nullptr;
  FetchAggregator* fetch_aggregator_;
  EventAggregator* event_aggregator_;
  BluetoothEvents* const bluetooth_events_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
