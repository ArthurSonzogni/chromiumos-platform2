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
