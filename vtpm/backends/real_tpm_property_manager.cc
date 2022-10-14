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
  is_sorted_ = false;
}

const std::vector<trunks::TPM_CC>& RealTpmPropertyManager::GetCommandList() {
  if (!is_sorted_) {
    std::sort(commands_.begin(), commands_.end());
    commands_.erase(std::unique(commands_.begin(), commands_.end()),
                    commands_.end());
    is_sorted_ = true;
  }
  return commands_;
}

}  // namespace vtpm
