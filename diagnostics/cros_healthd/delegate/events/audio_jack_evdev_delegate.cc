// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/audio_jack_evdev_delegate.h"

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <cstdint>
#include <string>
#include <utility>

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

AudioJackEvdevDelegate::AudioJackEvdevDelegate(
    mojo::PendingRemote<mojom::AudioJackObserver> observer)
    : observer_(std::move(observer)) {}

bool AudioJackEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  // Sarien board has separated event nodes so we use || instead of && here.
  return dev->HasEventCode(EV_SW, SW_HEADPHONE_INSERT) ||
         dev->HasEventCode(EV_SW, SW_MICROPHONE_INSERT);
}

void AudioJackEvdevDelegate::FireEvent(const input_event& ev,
                                       LibevdevWrapper* dev) {
  if (ev.type != EV_SW) {
    return;
  }

  if (ev.value == 1) {
    if (ev.code == SW_HEADPHONE_INSERT) {
      observer_->OnAdd(mojom::AudioJackEventInfo::DeviceType::kHeadphone);
    }
    if (ev.code == SW_MICROPHONE_INSERT) {
      observer_->OnAdd(mojom::AudioJackEventInfo::DeviceType::kMicrophone);
    }
  } else {
    if (ev.code == SW_HEADPHONE_INSERT) {
      observer_->OnRemove(mojom::AudioJackEventInfo::DeviceType::kHeadphone);
    }
    if (ev.code == SW_MICROPHONE_INSERT) {
      observer_->OnRemove(mojom::AudioJackEventInfo::DeviceType::kMicrophone);
    }
  }
}

void AudioJackEvdevDelegate::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void AudioJackEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {
  // Audio jack has no property to report.
}

}  // namespace diagnostics
