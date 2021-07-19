// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_CRYPTOHOME_CLIENT_H_
#define RMAD_SYSTEM_CRYPTOHOME_CLIENT_H_

#include <cstdint>

namespace rmad {

class CryptohomeClient {
 public:
  CryptohomeClient() = default;
  virtual ~CryptohomeClient() = default;

  virtual bool HasFwmp() = 0;
  virtual bool IsEnrolled() = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_CRYPTOHOME_CLIENT_H_
