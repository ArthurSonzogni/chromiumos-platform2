// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TIMEZONE_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TIMEZONE_FETCHER_H_

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The TimezoneFetcher class is responsible for gathering timezone info.
class TimezoneFetcher final {
 public:
  explicit TimezoneFetcher(Context* context);
  TimezoneFetcher(const TimezoneFetcher&) = delete;
  TimezoneFetcher& operator=(const TimezoneFetcher&) = delete;
  ~TimezoneFetcher() = default;

  // Returns a structure with either the device's timezone data or the error
  // that occurred fetching the information.
  chromeos::cros_healthd::mojom::TimezoneResultPtr FetchTimezoneInfo();

 private:
  // Unowned pointer that outlives this TimezoneFetcher instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TIMEZONE_FETCHER_H_
