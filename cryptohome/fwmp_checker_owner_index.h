// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FWMP_CHECKER_OWNER_INDEX_H_
#define CRYPTOHOME_FWMP_CHECKER_OWNER_INDEX_H_

#include "cryptohome/fwmp_checker.h"

namespace cryptohome {

class FwmpCheckerOwnerIndex : public FwmpChecker {
 public:
  FwmpCheckerOwnerIndex() = default;
  ~FwmpCheckerOwnerIndex() override = default;

  // For owner index, we don't have to perform the rationality check because in
  // practice we always re-create the index w/ the right attributes.
  bool IsValidForWrite(uint32_t) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FWMP_CHECKER_OWNER_INDEX_H_
