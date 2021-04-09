// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fwmp_checker_owner_index.h"

namespace cryptohome {

bool FwmpCheckerOwnerIndex::IsValidForWrite(uint32_t nv_index) {
  return true;
}
}  // namespace cryptohome
