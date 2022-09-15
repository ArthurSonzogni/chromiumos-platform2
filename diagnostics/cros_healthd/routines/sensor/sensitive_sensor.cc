// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/sensor/sensitive_sensor.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

SensitiveSensorRoutine::SensitiveSensorRoutine() = default;
SensitiveSensorRoutine::~SensitiveSensorRoutine() = default;

void SensitiveSensorRoutine::Start() {
  NOTIMPLEMENTED();
}

void SensitiveSensorRoutine::Resume() {
  NOTIMPLEMENTED();
}

void SensitiveSensorRoutine::Cancel() {
  NOTIMPLEMENTED();
}

void SensitiveSensorRoutine::PopulateStatusUpdate(
    mojom::RoutineUpdate* response, bool include_output) {
  NOTIMPLEMENTED();
}

mojom::DiagnosticRoutineStatusEnum SensitiveSensorRoutine::GetStatus() {
  return status_;
}

}  // namespace diagnostics
