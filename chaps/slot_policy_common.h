// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_SLOT_POLICY_COMMON_H_
#define CHAPS_SLOT_POLICY_COMMON_H_

#include "chaps/slot_policy_shared_slot.h"

#include "pkcs11/cryptoki.h"

namespace chaps {

// From NSS' pkcs11n.h.
inline constexpr CK_OBJECT_CLASS NSSCK_VENDOR_NSS = 0x4E534350;
inline constexpr CK_OBJECT_CLASS CKO_NSS =
    CKO_VENDOR_DEFINED | NSSCK_VENDOR_NSS;
inline constexpr CK_OBJECT_CLASS CKO_NSS_TRUST = CKO_NSS + 3;

}  // namespace chaps

#endif  // CHAPS_SLOT_POLICY_COMMON_H_
