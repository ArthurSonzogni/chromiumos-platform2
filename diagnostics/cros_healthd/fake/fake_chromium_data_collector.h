// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CHROMIUM_DATA_COLLECTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CHROMIUM_DATA_COLLECTOR_H_

#include <string>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/external/cros_healthd_internal.mojom.h"

namespace diagnostics {

// Fake implementation of ChromiumDataCollector.
class FakeChromiumDataCollector
    : public ash::cros_healthd::internal::mojom::ChromiumDataCollector {
 public:
  FakeChromiumDataCollector();
  FakeChromiumDataCollector(const FakeChromiumDataCollector&) = delete;
  FakeChromiumDataCollector& operator=(const FakeChromiumDataCollector&) =
      delete;
  ~FakeChromiumDataCollector() override;

  // Modifiers.
  mojo::Receiver<ash::cros_healthd::internal::mojom::ChromiumDataCollector>&
  receiver() {
    return receiver_;
  }

  std::vector<ash::cros_healthd::internal::mojom::TouchscreenDevicePtr>&
  touchscreen_devices() {
    return touchscreen_devices_;
  }

  std::string& touchpad_library_name() { return touchpad_library_name_; }

  void SetPrivacyScreenRequestProcessedBehaviour(
      base::OnceClosure on_receive_request,
      base::TimeDelta response_delay,
      bool response_value) {
    on_receive_privacy_screen_set_request_ = std::move(on_receive_request);
    privacy_screen_response_delay_ = response_delay;
    privacy_screen_request_processed_ = response_value;
  }

  void SetAudioOutputMuteRequestResult(bool expected_result) {
    audio_output_mute_request_result_ = expected_result;
  }

 private:
  // `ash::cros_healthd::internal::mojom::ChromiumDataCollector` overrides.
  void GetTouchscreenDevices(GetTouchscreenDevicesCallback callback) override;
  void GetTouchpadLibraryName(GetTouchpadLibraryNameCallback callback) override;
  void SetPrivacyScreenState(bool state,
                             SetPrivacyScreenStateCallback callback) override;
  void SetAudioOutputMute(bool mute_on,
                          SetAudioOutputMuteCallback callback) override;

  // Mojo receiver for binding pipe.
  mojo::Receiver<ash::cros_healthd::internal::mojom::ChromiumDataCollector>
      receiver_;
  // Expected touchscreen devices.
  std::vector<ash::cros_healthd::internal::mojom::TouchscreenDevicePtr>
      touchscreen_devices_;
  // Expected touchpad library name.
  std::string touchpad_library_name_;
  // Runnable function when browser receive privacy screen request.
  base::OnceClosure on_receive_privacy_screen_set_request_;
  // Delay after browser receives request and before responses to client.
  base::TimeDelta privacy_screen_response_delay_;
  // Expected result of processing privacy screen request.
  bool privacy_screen_request_processed_;
  // Expected audio output mute request result.
  bool audio_output_mute_request_result_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CHROMIUM_DATA_COLLECTOR_H_
