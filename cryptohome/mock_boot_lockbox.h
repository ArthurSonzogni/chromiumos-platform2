// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_BOOT_LOCKBOX_H_
#define CRYPTOHOME_MOCK_BOOT_LOCKBOX_H_

#include "cryptohome/boot_lockbox.h"

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockBootLockbox : public BootLockbox {
 public:
  MockBootLockbox() : BootLockbox(NULL, NULL, NULL) {}
  virtual ~MockBootLockbox() {}

  MOCK_METHOD2(Sign, bool(const brillo::SecureBlob&, brillo::SecureBlob*));
  MOCK_METHOD2(Verify, bool(const brillo::SecureBlob&,
                            const brillo::SecureBlob&));
  MOCK_METHOD0(FinalizeBoot, bool());
  MOCK_METHOD0(IsFinalized, bool());
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_BOOT_LOCKBOX_H_
