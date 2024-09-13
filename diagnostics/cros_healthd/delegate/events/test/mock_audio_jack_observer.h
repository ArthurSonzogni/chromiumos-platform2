// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_AUDIO_JACK_OBSERVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_AUDIO_JACK_OBSERVER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics::test {

class MockAudioJackObserver
    : public ::ash::cros_healthd::mojom::AudioJackObserver {
 public:
  // ::ash::cros_healthd::mojom::AudioJackObserver overrides:
  MOCK_METHOD(void,
              OnAdd,
              (::ash::cros_healthd::mojom::AudioJackEventInfo::DeviceType),
              (override));
  MOCK_METHOD(void,
              OnRemove,
              (::ash::cros_healthd::mojom::AudioJackEventInfo::DeviceType),
              (override));
};

}  // namespace diagnostics::test

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_AUDIO_JACK_OBSERVER_H_
