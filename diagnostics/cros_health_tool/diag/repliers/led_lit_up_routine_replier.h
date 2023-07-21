// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_REPLIERS_LED_LIT_UP_ROUTINE_REPLIER_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_REPLIERS_LED_LIT_UP_ROUTINE_REPLIER_H_

#include <diagnostics/mojom/public/cros_healthd_routines.mojom.h>
#include <mojo/public/cpp/bindings/receiver.h>

namespace diagnostics {

class LedLitUpRoutineReplier
    : public ash::cros_healthd::mojom::LedLitUpRoutineReplier {
 public:
  LedLitUpRoutineReplier() = default;
  LedLitUpRoutineReplier(const LedLitUpRoutineReplier&) = delete;
  LedLitUpRoutineReplier& operator=(const LedLitUpRoutineReplier&) = delete;
  ~LedLitUpRoutineReplier() = default;

  // ash::cros_healthd::mojom::LedLitUpRoutineReplier overrides:
  void GetColorMatched(GetColorMatchedCallback callback);

  mojo::PendingRemote<ash::cros_healthd::mojom::LedLitUpRoutineReplier>
  BindNewPipdAndPassRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

 private:
  mojo::Receiver<ash::cros_healthd::mojom::LedLitUpRoutineReplier> receiver_{
      this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_DIAG_REPLIERS_LED_LIT_UP_ROUTINE_REPLIER_H_
