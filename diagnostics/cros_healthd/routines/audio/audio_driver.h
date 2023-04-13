// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AUDIO_AUDIO_DRIVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AUDIO_AUDIO_DRIVER_H_

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

// The audio driver routine checks that the device's audio driver is working
// correctly.
class AudioDriverRoutine final : public BaseRoutineControl {
 public:
  explicit AudioDriverRoutine(
      Context* context,
      const ash::cros_healthd::mojom::AudioDriverRoutineArgumentPtr& arg);
  AudioDriverRoutine(const AudioDriverRoutine&) = delete;
  AudioDriverRoutine& operator=(const AudioDriverRoutine&) = delete;
  ~AudioDriverRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  // Check if CRAS can detect at least one internal audio card.
  bool CheckInternalCardDetected();

  // Check if all audio devices succeed to open. It returns false as long as any
  // of the audio device fails to open.
  bool CheckAudioDevicesSucceedToOpen();

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AUDIO_AUDIO_DRIVER_H_
