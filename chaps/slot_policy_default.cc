// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/slot_policy_default.h"

namespace chaps {

SlotPolicyDefault::SlotPolicyDefault() = default;

SlotPolicyDefault::~SlotPolicyDefault() = default;

bool SlotPolicyDefault::IsObjectClassAllowedForNewObject(
    CK_OBJECT_CLASS object_class) {
  // TODO(https://crbug.com/1132030): Disallow NSS trust objects.
  return true;
}

bool SlotPolicyDefault::IsObjectClassAllowedForImportedObject(
    CK_OBJECT_CLASS object_class) {
  return true;
}

}  // namespace chaps
