// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINE_ADAPTER_H_

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <mojo/public/cpp/bindings/unique_receiver_set.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/routines/routine_service.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

class RoutineAdapter : public DiagnosticRoutine,
                       ash::cros_healthd::mojom::RoutineObserver {
 public:
  explicit RoutineAdapter(
      ash::cros_healthd::mojom::RoutineArgument::Tag routine_type);
  RoutineAdapter(const RoutineAdapter&) = delete;
  RoutineAdapter& operator=(const RoutineAdapter&) = delete;
  ~RoutineAdapter();

  // ash::cros_healthd::mojom::RoutineObserver override.
  void OnRoutineStateChange(
      ash::cros_healthd::mojom::RoutineStatePtr state) override;

  // DiagnosticRoutine override.
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(ash::cros_healthd::mojom::RoutineUpdate* response,
                            bool include_output) override;
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus() override;
  void RegisterStatusChangedCallback(StatusChangedCallback callback) override;

  // Sets up the adapter and calls to `CreateRoutine`. After calling this
  // method, it is safe to call `Start`.
  void SetupAdapter(
      ash::cros_healthd::mojom::RoutineArgumentPtr arg,
      ash::cros_healthd::mojom::CrosHealthdRoutinesService* routine_service);

  // Bind the remote for the routine control to a new pipe and return the
  // receiver.
  std::tuple<mojo::PendingReceiver<ash::cros_healthd::mojom::RoutineControl>,
             mojo::PendingRemote<ash::cros_healthd::mojom::RoutineObserver>>
  SetupRoutineControlAndObserver();

  // Exported for testing only.
  void FlushRoutineControlForTesting();
  mojo::Remote<ash::cros_healthd::mojom::RoutineControl>& routine_control();

 private:
  // Sets error message when routine disconnects.
  void OnRoutineDisconnect(uint32_t custom_reason, const std::string& message);
  // Notifies each of |status_changed_callbacks_| when the status changes.
  void NotifyStatusChanged(
      ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status);

  // Holds the remote to communicate with the routine.
  mojo::Remote<ash::cros_healthd::mojom::RoutineControl> routine_control_;
  // A receiver that will let this class acts as the routine observer.
  mojo::Receiver<ash::cros_healthd::mojom::RoutineObserver> observer_receiver_{
      this};
  // States are cached for querying by diagnostics service.
  ash::cros_healthd::mojom::RoutineStatePtr cached_state_;
  // Whether an error has occurred.
  bool error_occured_;
  // Whether the routine has been cancelled.
  bool routine_cancelled_;
  // Used to record down what routine is running on this routine adapter.
  ash::cros_healthd::mojom::RoutineArgument::Tag routine_type_;
  // The message for the error if error has occurred.
  std::string error_message_;
  // Callbacks to invoke when the status changes.
  std::vector<StatusChangedCallback> status_changed_callbacks_;
  // Previous status of the routine, used for avoiding duplicate status in UMA.
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum last_status_;

  // Must be the last class member.
  base::WeakPtrFactory<RoutineAdapter> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINE_ADAPTER_H_
