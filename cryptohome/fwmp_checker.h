// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FWMP_CHECKER_H_
#define CRYPTOHOME_FWMP_CHECKER_H_

#include <cstdint>

namespace cryptohome {

class FwmpChecker {
 public:
  FwmpChecker() = default;
  virtual ~FwmpChecker() = default;

  virtual bool IsValidForWrite(uint32_t nv_index) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FWMP_CHECKER_H_
