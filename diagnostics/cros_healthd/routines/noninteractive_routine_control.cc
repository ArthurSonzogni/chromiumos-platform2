// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/noninteractive_routine_control.h"

#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace {

namespace mojom = ash::cros_healthd::mojom;

}  // namespace

namespace diagnostics {

NoninteractiveRoutineControl::NoninteractiveRoutineControl() = default;

NoninteractiveRoutineControl::~NoninteractiveRoutineControl() = default;

void NoninteractiveRoutineControl::ReplyInquiry(
    mojom::RoutineInquiryReplyPtr /*reply*/) {
  RaiseException("Reply does not match the inquiry");
}

}  // namespace diagnostics
