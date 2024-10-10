// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_enforcement_policy.h"

#include <base/logging.h>

namespace arc::keymint::context {

ArcEnforcementPolicy::ArcEnforcementPolicy(uint32_t max_access_time_map_size,
                                           uint32_t max_access_count_map_size)
    : SoftKeymasterEnforcement(max_access_time_map_size,
                               max_access_count_map_size) {}

ArcEnforcementPolicy::~ArcEnforcementPolicy() = default;

::keymaster::KmErrorOr<std::array<uint8_t, 32>>
ArcEnforcementPolicy::ComputeHmac(const std::vector<uint8_t>&) const {
  return KM_ERROR_UNIMPLEMENTED;
}

}  // namespace arc::keymint::context
