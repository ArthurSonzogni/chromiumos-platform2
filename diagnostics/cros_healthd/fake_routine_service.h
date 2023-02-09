// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_ROUTINE_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_ROUTINE_SERVICE_H_

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

// Implementation of CrosHealthdRoutinesService that should only be used for
// testing.
class FakeRoutineService final
    : public ash::cros_healthd::mojom::CrosHealthdRoutinesService {
 public:
  FakeRoutineService() = default;
  FakeRoutineService(const FakeRoutineService&) = delete;
  FakeRoutineService& operator=(const FakeRoutineService&) = delete;
  ~FakeRoutineService() = default;

  // ash::cros_healthd::mojom::CrosHealthdRoutinesService override.
  void CreateRoutine(
      ash::cros_healthd::mojom::RoutineArgumentPtr routine_arg,
      mojo::PendingReceiver<ash::cros_healthd::mojom::RoutineControl>
          routine_receiver) override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_ROUTINE_SERVICE_H_
