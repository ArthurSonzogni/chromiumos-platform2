// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_ROUTINE_V2_CLIENT_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_ROUTINE_V2_CLIENT_H_

#include <string>

#include <base/memory/weak_ptr.h>
#include <base/run_loop.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_health_tool/diag/observers/routine_observer.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

// RoutineV2Client interacts with CrosHealthdRoutinesService to access routines.
// When |single_line_json| is true, the output JSON string will be printed in a
// single line.
class RoutineV2Client {
 public:
  explicit RoutineV2Client(
      mojo::Remote<ash::cros_healthd::mojom::CrosHealthdRoutinesService>
          routine_service,
      bool single_line_json);
  RoutineV2Client(const RoutineV2Client&) = delete;
  RoutineV2Client& operator=(const RoutineV2Client&) = delete;
  ~RoutineV2Client();

  // Creates a routine with the given |argument|.
  void CreateRoutine(ash::cros_healthd::mojom::RoutineArgumentPtr argument);

  // Starts the created routine and wait until either the routine finishes or
  // an exception occurs.
  void StartAndWaitUntilTerminated();

 private:
  void OnRoutineDisconnection(uint32_t error, const std::string& message);

  // The remote for CrosHealthdRoutinesService.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdRoutinesService>
      routine_service_;
  // Whether to print the output JSON string in a single line.
  bool single_line_json_ = false;
  // This mojo remote controls the routine.
  mojo::Remote<ash::cros_healthd::mojom::RoutineControl> routine_control_;
  // Used for waiting until terminated.
  base::RunLoop run_loop_;
  // The observer for handling routine state updates.
  RoutineObserver observer_{run_loop_.QuitClosure()};
  // Must be the last member of the class.
  base::WeakPtrFactory<RoutineV2Client> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_ROUTINE_V2_CLIENT_H_
