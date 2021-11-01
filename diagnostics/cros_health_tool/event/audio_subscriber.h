// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_AUDIO_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_AUDIO_SUBSCRIBER_H_

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

// This class subscribes to cros_healthd's audio notifications and outputs
// received notifications to stdout.
class AudioSubscriber final
    : public chromeos::cros_healthd::mojom::CrosHealthdAudioObserver {
 public:
  explicit AudioSubscriber(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdAudioObserver> receiver);
  AudioSubscriber(const AudioSubscriber&) = delete;
  AudioSubscriber& operator=(const AudioSubscriber&) = delete;
  ~AudioSubscriber();

  // chromeos::cros_healthd::mojom::CrosHealthdAudioObserver overrides:
  void OnUnderrun() override;
  void OnSevereUnderrun() override;

 private:
  // Allows the remote cros_healthd to call AudioSubscriber's
  // CrosHealthdAudioObserver methods.
  const mojo::Receiver<chromeos::cros_healthd::mojom::CrosHealthdAudioObserver>
      receiver_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_AUDIO_SUBSCRIBER_H_
