// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_AUDIO_EVENTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_AUDIO_EVENTS_H_

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// Interface which allows clients to subscribe to audio-related events.
class AudioEvents {
 public:
  virtual ~AudioEvents() = default;

  // Adds a new observer to be notified when audio-related events occur.
  virtual void AddObserver(
      mojo::PendingRemote<
          chromeos::cros_healthd::mojom::CrosHealthdAudioObserver>
          observer) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_AUDIO_EVENTS_H_
