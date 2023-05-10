// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_

#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"

namespace diagnostics {

class GroundTruth final {
 public:
  GroundTruth();
  GroundTruth(const GroundTruth&) = delete;
  GroundTruth& operator=(const GroundTruth&) = delete;
  ~GroundTruth();

  ash::cros_healthd::mojom::SupportStatusPtr GetEventSupportStatus(
      ash::cros_healthd::mojom::EventCategoryEnum category);
  void IsEventSupported(ash::cros_healthd::mojom::EventCategoryEnum category,
                        ash::cros_healthd::mojom::CrosHealthdEventService::
                            IsEventSupportedCallback callback);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_
