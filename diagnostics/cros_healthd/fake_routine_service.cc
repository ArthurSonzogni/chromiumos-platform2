// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake_routine_service.h"
#include "base/notreached.h"

namespace diagnostics {

void FakeRoutineService::CreateRoutine(
    ash::cros_healthd::mojom::RoutineArgumentPtr routine_arg,
    mojo::PendingReceiver<ash::cros_healthd::mojom::RoutineControl>
        routine_receiver) {
  NOTIMPLEMENTED();
  return;
}

}  // namespace diagnostics
