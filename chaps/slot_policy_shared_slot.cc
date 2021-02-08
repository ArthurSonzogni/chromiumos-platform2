// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/slot_policy_shared_slot.h"

#include "chaps/slot_policy_common.h"

namespace chaps {

SlotPolicySharedSlot::SlotPolicySharedSlot() = default;

SlotPolicySharedSlot::~SlotPolicySharedSlot() = default;

bool SlotPolicySharedSlot::IsObjectClassAllowedForNewObject(
    CK_OBJECT_CLASS object_class) {
  // TODO(https://crbug.com/1132030): Disallow NSS trust objects.
  return true;
}

bool SlotPolicySharedSlot::IsObjectClassAllowedForImportedObject(
    CK_OBJECT_CLASS object_class) {
  // TODO(https://crbug.com/1132030): Disallow NSS trust objects.
  return true;
}

}  // namespace chaps
