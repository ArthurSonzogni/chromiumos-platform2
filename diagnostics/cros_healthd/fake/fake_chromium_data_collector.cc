// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_chromium_data_collector.h"

#include <utility>

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
    bool state, SetPrivacyScreenStateCallback callback) {
  // Assumed browser accepts the request.
  std::move(callback).Run(true);
}

}  // namespace diagnostics
