// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_NETWORK_BANDWIDTH_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_NETWORK_BANDWIDTH_H_

#include <optional>
#include <string>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/executor/utils/scoped_process_control.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom-forward.h"
#include "diagnostics/cros_healthd/routines/noninteractive_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {
class Context;

// The maximum runtime for each test is 14 seconds in libndt7. Use a longer
// timeout here.
constexpr base::TimeDelta kRunningNdtTimeout = base::Seconds(15);

// The network bandwidth routine checks network bandwidth by sequentially
// executing a download test and an upload test.
class NetworkBandwidthRoutine final
    : public NoninteractiveRoutineControl,
      ash::cros_healthd::mojom::NetworkBandwidthObserver {
 public:
  explicit NetworkBandwidthRoutine(
      Context* context,
      const ash::cros_healthd::mojom::NetworkBandwidthRoutineArgumentPtr& arg);

  NetworkBandwidthRoutine(const NetworkBandwidthRoutine&) = delete;
  NetworkBandwidthRoutine& operator=(const NetworkBandwidthRoutine&) = delete;
  ~NetworkBandwidthRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

  // ash::cros_healthd::mojom::NetworkBandwidthObserver overrides:
  void OnProgress(double speed_kbps, double percentage) override;

 private:
  void RunNextStep();

  // Handle the response of running bandwidth test.
  void HandleBandwidthTestResponse(std::optional<double> average_speed);

  // Set up the timeout callback.
  void SetupTimeoutCallback();

  // Routine timeout function.
  void OnTimeoutOccurred();

  // Set the routine result and stop other callbacks.
  void SetResultAndStop(std::optional<std::string> error);

  enum class TestStep : int32_t {
    kInitialize = 0,
    kDownload = 1,
    kUpload = 2,
    kComplete = 3,  // Should be the last one. New step should be added before
                    // it.
  };
  TestStep step_ = TestStep::kInitialize;

  // Unowned pointer that should outlive this instance.
  Context* const context_;

  // The scoped version of process controls that manages the lifetime of the
  // delegate process that runs network bandwidth tests.
  ScopedProcessControl scoped_process_control_upload_;
  ScopedProcessControl scoped_process_control_download_;

  // Receiver for observing the progress of network bandwidth test.
  mojo::Receiver<ash::cros_healthd::mojom::NetworkBandwidthObserver> receiver_{
      this};

  // The callback to stop test and report failure on timeout.
  base::CancelableOnceClosure timeout_callback_;

  // Detail of routine output.
  ash::cros_healthd::mojom::NetworkBandwidthRoutineDetailPtr routine_output_;

  // Must be the last class member.
  base::WeakPtrFactory<NetworkBandwidthRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_NETWORK_BANDWIDTH_H_
