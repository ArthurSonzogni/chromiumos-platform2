// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/audio_subscriber.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/check.h>

namespace diagnostics {

AudioSubscriber::AudioSubscriber(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdAudioObserver> receiver)
    : receiver_{this /* impl */, std::move(receiver)} {
  DCHECK(receiver_.is_bound());
}

AudioSubscriber::~AudioSubscriber() = default;

void AudioSubscriber::OnUnderrun() {
  std::cout << "Receive audio underrun event" << std::endl;
}

void AudioSubscriber::OnSevereUnderrun() {
  std::cout << "Receive audio severe underrun event" << std::endl;
}

}  // namespace diagnostics
