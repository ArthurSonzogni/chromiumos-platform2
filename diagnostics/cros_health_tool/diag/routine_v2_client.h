// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_ROUTINE_V2_CLIENT_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_ROUTINE_V2_CLIENT_H_

#include <string>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/run_loop.h>
#include <base/values.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

// RoutineV2Client interacts with CrosHealthdRoutinesService to access routines.
// When |single_line_json| is true, the output JSON string will be printed in a
// single line. Otherwise, it will pretty-print the JSON string.
class RoutineV2Client : public ash::cros_healthd::mojom::RoutineObserver {
 public:
  explicit RoutineV2Client(
      mojo::Remote<ash::cros_healthd::mojom::CrosHealthdRoutinesService>
          routine_service,
      bool single_line_json);
  RoutineV2Client(const RoutineV2Client&) = delete;
  RoutineV2Client& operator=(const RoutineV2Client&) = delete;
  ~RoutineV2Client();

  // ash::cros_healthd::mojom::RoutineObserver override.
  void OnRoutineStateChange(
      ash::cros_healthd::mojom::RoutineStatePtr state) override;

  // Creates a routine with the given |argument|.
  void CreateRoutine(ash::cros_healthd::mojom::RoutineArgumentPtr argument);

  // Starts the created routine and wait until either the routine finishes or
  // an exception occurs.
  void StartAndWaitUntilTerminated();

 private:
  void OnRoutineDisconnection(uint32_t error, const std::string& message);
  void PrintOutput(const base::Value::Dict& output);

  // The remote for CrosHealthdRoutinesService.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdRoutinesService>
      routine_service_;
  // The callback for printing the output.
  base::RepeatingCallback<void(const base::Value::Dict&)> output_printer_;
  // This mojo remote controls the routine.
  mojo::Remote<ash::cros_healthd::mojom::RoutineControl> routine_control_;
  // Used for waiting until terminated.
  base::RunLoop run_loop_;
  // Allows the remote to call RoutineObserver methods.
  mojo::Receiver<ash::cros_healthd::mojom::RoutineObserver> receiver_{this};
  // Must be the last member of the class.
  base::WeakPtrFactory<RoutineV2Client> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_ROUTINE_V2_CLIENT_H_
