// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_

#include <cstdint>
#include <vector>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "diagnostics/cros_healthd/events/audio_events.h"
#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/events/lid_events.h"
#include "diagnostics/cros_healthd/events/power_events.h"
#include "diagnostics/cros_healthd/fetch_aggregator.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/network_health.mojom.h"

namespace diagnostics {

// Implements the "CrosHealthdService" Mojo interface exposed by the
// cros_healthd daemon (see the API definition at mojo/cros_healthd.mojom)
class CrosHealthdMojoService final
    : public chromeos::cros_healthd::mojom::CrosHealthdEventService,
      public chromeos::cros_healthd::mojom::CrosHealthdProbeService,
      public chromeos::cros_healthd::mojom::CrosHealthdSystemService {
 public:
  using ProbeCategoryEnum = chromeos::cros_healthd::mojom::ProbeCategoryEnum;

  // |fetch_aggregator| - responsible for fulfilling probe requests.
  // |bluetooth_events| - BluetoothEvents implementation.
  // |lid_events| - LidEvents implementation.
  // |power_events| - PowerEvents implementation.
  // |audio_events| - AudioEvents implementation.
  CrosHealthdMojoService(Context* context,
                         FetchAggregator* fetch_aggregator,
                         BluetoothEvents* bluetooth_events,
                         LidEvents* lid_events,
                         PowerEvents* power_events,
                         AudioEvents* audio_events);
  CrosHealthdMojoService(const CrosHealthdMojoService&) = delete;
  CrosHealthdMojoService& operator=(const CrosHealthdMojoService&) = delete;
  ~CrosHealthdMojoService() override;

  // chromeos::cros_healthd::mojom::CrosHealthdEventService overrides:
  void AddBluetoothObserver(
      mojo::PendingRemote<
          chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver> observer)
      override;
  void AddLidObserver(
      mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdLidObserver>
          observer) override;
  void AddPowerObserver(mojo::PendingRemote<
                        chromeos::cros_healthd::mojom::CrosHealthdPowerObserver>
                            observer) override;
  void AddNetworkObserver(
      mojo::PendingRemote<
          chromeos::network_health::mojom::NetworkEventsObserver> observer)
      override;
  void AddAudioObserver(mojo::PendingRemote<
                        chromeos::cros_healthd::mojom::CrosHealthdAudioObserver>
                            observer) override;

  // chromeos::cros_healthd::mojom::CrosHealthdProbeService overrides:
  void ProbeProcessInfo(uint32_t process_id,
                        ProbeProcessInfoCallback callback) override;
  void ProbeTelemetryInfo(const std::vector<ProbeCategoryEnum>& categories,
                          ProbeTelemetryInfoCallback callback) override;

  // chromeos::cros_healthd::mojom::CrosHealthdSystemService overrides:
  void GetServiceStatus(GetServiceStatusCallback callback) override;

  // Adds a new binding to the internal binding sets.
  void AddProbeReceiver(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdProbeService> receiver);
  void AddEventReceiver(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdEventService> receiver);
  void AddSystemReceiver(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdSystemService> receiver);

 private:
  // Mojo binding sets that connect |this| with message pipes, allowing the
  // remote ends to call our methods.
  mojo::ReceiverSet<chromeos::cros_healthd::mojom::CrosHealthdProbeService>
      probe_receiver_set_;
  mojo::ReceiverSet<chromeos::cros_healthd::mojom::CrosHealthdEventService>
      event_receiver_set_;
  mojo::ReceiverSet<chromeos::cros_healthd::mojom::CrosHealthdSystemService>
      system_receiver_set_;

  // Unowned. The following instances should outlive this instance.
  Context* const context_ = nullptr;
  FetchAggregator* fetch_aggregator_;
  BluetoothEvents* const bluetooth_events_ = nullptr;
  LidEvents* const lid_events_ = nullptr;
  PowerEvents* const power_events_ = nullptr;
  AudioEvents* const audio_events_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
