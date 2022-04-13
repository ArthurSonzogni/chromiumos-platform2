// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CHROMIUM_DATA_COLLECTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CHROMIUM_DATA_COLLECTOR_H_

#include <string>
#include <vector>

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

 private:
  // `ash::cros_healthd::internal::mojom::ChromiumDataCollector` overrides.
  void GetTouchscreenDevices(GetTouchscreenDevicesCallback callback) override;
  void GetTouchpadLibraryName(GetTouchpadLibraryNameCallback callback) override;

  // Mojo receiver for binding pipe.
  mojo::Receiver<ash::cros_healthd::internal::mojom::ChromiumDataCollector>
      receiver_;
  // Expected touchscreen devices.
  std::vector<ash::cros_healthd::internal::mojom::TouchscreenDevicePtr>
      touchscreen_devices_;
  // Expected touchpad library name.
  std::string touchpad_library_name_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CHROMIUM_DATA_COLLECTOR_H_
