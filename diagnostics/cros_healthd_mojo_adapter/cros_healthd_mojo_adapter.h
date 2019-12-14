// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_

#include <memory>
#include <vector>

#include <base/macros.h>
#include <base/threading/thread.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Provides a mojo connection to cros_healthd. See mojo/cros_healthd.mojom for
// details on cros_healthd's mojo interface. This should only be used by
// processes whose only mojo connection is to cros_healthd.
class CrosHealthdMojoAdapter final {
 public:
  CrosHealthdMojoAdapter();
  ~CrosHealthdMojoAdapter();

  // Gets telemetry information from cros_healthd.
  chromeos::cros_healthd::mojom::TelemetryInfoPtr GetTelemetryInfo(
      const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
          categories_to_probe);

  // Runs the urandom routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunUrandomRoutine(
      uint32_t length_seconds);

  // Runs the battery capacity routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryCapacityRoutine(uint32_t low_mah, uint32_t high_mah);

  // Runs the battery health routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunBatteryHealthRoutine(
      uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed);

  // Runs the smartctl-check routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunSmartctlCheckRoutine();

  // Returns which routines are available on the platform.
  std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
  GetAvailableRoutines();

  // Gets an update for the specified routine.
  chromeos::cros_healthd::mojom::RoutineUpdatePtr GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output);

 private:
  // Establishes a mojo connection with cros_healthd.
  void Connect();

  // IPC threads.
  base::Thread mojo_thread_{"Mojo Thread"};
  base::Thread dbus_thread_{"D-Bus Thread"};

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  // Used to send mojo requests to cros_healthd.
  chromeos::cros_healthd::mojom::CrosHealthdServicePtr cros_healthd_service_;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthdMojoAdapter);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
