// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_ssfc_utils.h"

#include <string>

#include <base/check.h>

namespace rmad {
namespace fake {

bool FakeSsfcUtils::GetSSFC(const std::string& model,
                            bool* need_to_update,
                            uint32_t* ssfc) const {
  CHECK(need_to_update);
  CHECK(ssfc);

  *need_to_update = false;
  return true;
}

}  // namespace fake
}  // namespace rmad
