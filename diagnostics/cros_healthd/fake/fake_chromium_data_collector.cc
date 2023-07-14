// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_chromium_data_collector.h"

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/time/time.h>

namespace diagnostics {

namespace internal_mojom = ::ash::cros_healthd::internal::mojom;

FakeChromiumDataCollector::FakeChromiumDataCollector() : receiver_(this) {}

FakeChromiumDataCollector::~FakeChromiumDataCollector() = default;

void FakeChromiumDataCollector::GetTouchscreenDevices(
    GetTouchscreenDevicesCallback callback) {
  std::vector<internal_mojom::TouchscreenDevicePtr> res;
  for (const internal_mojom::TouchscreenDevicePtr& item :
       touchscreen_devices_) {
    res.push_back(item.Clone());
  }
  std::move(callback).Run(std::move(res));
}

void FakeChromiumDataCollector::GetTouchpadLibraryName(
    GetTouchpadLibraryNameCallback callback) {
  std::move(callback).Run(touchpad_library_name_);
}

void FakeChromiumDataCollector::SetPrivacyScreenState(
    bool target_state, SetPrivacyScreenStateCallback callback) {
  CHECK(!on_receive_privacy_screen_set_request_.is_null());
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      std::move(on_receive_privacy_screen_set_request_)
          .Then(base::BindOnce(std::move(callback),
                               privacy_screen_request_processed_)),
      privacy_screen_response_delay_);
}

void FakeChromiumDataCollector::SetAudioOutputMute(
    bool mute_on, SetAudioOutputMuteCallback callback) {
  std::move(callback).Run(audio_output_mute_request_result_);
}

}  // namespace diagnostics
