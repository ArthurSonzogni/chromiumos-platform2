// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/real_tpm_property_manager.h"

#include <algorithm>
#include <vector>

#include <trunks/tpm_generated.h>

namespace vtpm {

void RealTpmPropertyManager::AddCommand(trunks::TPM_CC cc) {
  commands_.emplace_back(cc);
  commands_is_sorted_ = false;
}

const std::vector<trunks::TPM_CC>& RealTpmPropertyManager::GetCommandList() {
  if (!commands_is_sorted_) {
    std::sort(commands_.begin(), commands_.end());
    commands_.erase(std::unique(commands_.begin(), commands_.end()),
                    commands_.end());
    commands_is_sorted_ = true;
  }
  return commands_;
}

void RealTpmPropertyManager::AddCapabilityProperty(trunks::TPM_PT property,
                                                   trunks::UINT32 value) {
  capability_properties_.emplace_back(
      trunks::TPMS_TAGGED_PROPERTY{property, value});
  capability_properties_is_sorted_ = false;
}

const std::vector<trunks::TPMS_TAGGED_PROPERTY>&
RealTpmPropertyManager::GetCapabilityPropertyList() {
  if (!capability_properties_is_sorted_) {
    std::sort(capability_properties_.begin(), capability_properties_.end(),
              [](const trunks::TPMS_TAGGED_PROPERTY& a,
                 const trunks::TPMS_TAGGED_PROPERTY& b) -> bool {
                return a.property < b.property;
              });
    capability_properties_is_sorted_ = true;
  }
  return capability_properties_;
}

}  // namespace vtpm
