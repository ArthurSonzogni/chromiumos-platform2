// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/audio/audio_set_volume.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <cras/dbus-proxies.h>

#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

AudioSetVolumeRoutine::AudioSetVolumeRoutine(Context* context,
                                             uint64_t node_id,
                                             uint8_t volume,
                                             bool mute_on)
    : node_id_(node_id),
      volume_(volume),
      mute_on_(mute_on),
      context_(context),
      status_(mojom::DiagnosticRoutineStatusEnum::kReady) {
  volume_ = std::min(volume_, (uint8_t)100);
}

AudioSetVolumeRoutine::~AudioSetVolumeRoutine() = default;

void AudioSetVolumeRoutine::Start() {
  status_ = mojom::DiagnosticRoutineStatusEnum::kRunning;
  brillo::ErrorPtr error;

  if (!context_->cras_proxy()->SetOutputUserMute(mute_on_, &error)) {
    LOG(ERROR) << "Failed to set output user mute: " << error->GetMessage();
    status_ = mojom::DiagnosticRoutineStatusEnum::kError;
    status_message_ = "Failed to set output user mute";
    return;
  }
  if (!context_->cras_proxy()->SetOutputNodeVolume(node_id_, volume_, &error)) {
    LOG(ERROR) << "Failed to set audio active output node[" << node_id_
               << "] to volume[" << volume_ << "]: " << error->GetMessage();
    status_ = mojom::DiagnosticRoutineStatusEnum::kError;
    status_message_ = "Failed to set audio active output node volume";
    return;
  }

  status_ = mojom::DiagnosticRoutineStatusEnum::kPassed;
}

void AudioSetVolumeRoutine::Resume() {}

void AudioSetVolumeRoutine::Cancel() {}

void AudioSetVolumeRoutine::PopulateStatusUpdate(mojom::RoutineUpdate* response,
                                                 bool include_output) {
  auto update = mojom::NonInteractiveRoutineUpdate::New();
  update->status = status_;
  update->status_message = status_message_;
  response->routine_update_union =
      mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
  if (status_ == mojom::DiagnosticRoutineStatusEnum::kReady ||
      status_ == mojom::DiagnosticRoutineStatusEnum::kRunning) {
    response->progress_percent = 0;
  } else {
    response->progress_percent = 100;
  }
}

mojom::DiagnosticRoutineStatusEnum AudioSetVolumeRoutine::GetStatus() {
  return status_;
}

}  // namespace diagnostics
