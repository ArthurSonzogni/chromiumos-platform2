// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/audio/audio_driver.h"

#include <utility>
#include <vector>

#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <cras/dbus-proxies.h>

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

AudioDriverRoutine::AudioDriverRoutine(
    Context* context, const mojom::AudioDriverRoutineArgumentPtr& arg)
    : context_(context) {
  CHECK(context_);
}

AudioDriverRoutine::~AudioDriverRoutine() = default;

void AudioDriverRoutine::OnStart() {
  SetRunningState();
  auto detail = mojom::AudioDriverRoutineDetail::New();

  detail->internal_card_detected = CheckInternalCardDetected();
  SetPercentage(50);

  detail->audio_devices_succeed_to_open = CheckAudioDevicesSucceedToOpen();

  bool result =
      detail->internal_card_detected && detail->audio_devices_succeed_to_open;
  SetFinishedState(result,
                   mojom::RoutineDetail::NewAudioDriver(std::move(detail)));
}

bool AudioDriverRoutine::CheckInternalCardDetected() {
  bool detected = false;
  brillo::ErrorPtr error;
  if (!context_->cras_proxy()->IsInternalCardDetected(&detected, &error)) {
    RaiseException("Failed to get detected internal card from cras: " +
                   error->GetMessage());
    return false;
  }

  return detected;
}

bool AudioDriverRoutine::CheckAudioDevicesSucceedToOpen() {
  std::vector<brillo::VariantDictionary> nodes;
  brillo::ErrorPtr error;
  if (!context_->cras_proxy()->GetNodeInfos(&nodes, &error)) {
    RaiseException("Failed retrieving node info from cras: " +
                   error->GetMessage());
    return false;
  }

  for (const auto& node : nodes) {
    auto open_result = brillo::GetVariantValueOrDefault<uint32_t>(
        node, cras::kDeviceLastOpenResultProperty);
    // Open result:
    // 0 - Unknown - Default value. If CRAS doesn't open the device, hence there
    //               is no success or failure, then CRAS reports Unknown to the
    //               callers.
    // 1 - Success
    // 2 - Failure
    if (open_result == 2) {
      return false;
    }
  }

  return true;
}

}  // namespace diagnostics
