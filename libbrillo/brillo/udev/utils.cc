// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/udev/utils.h"

#include <memory>

namespace brillo {

namespace {

constexpr char kRemovableAttr[] = "removable";

bool ContainsRemovableAttribute(const brillo::UdevDevice& device) {
  const char* value = device.GetSysAttributeValue(kRemovableAttr);
  return value && strncmp(value, "1", 1) == 0;
}

}  // namespace

bool IsRemovable(const brillo::UdevDevice& device) {
  if (ContainsRemovableAttribute(device)) {
    return true;
  }
  // Check if any of the parents are removable. From USB devices the parent node
  // `/dev/sda` is removable, while a node like `/dev/sda1` would not have this
  // property.
  for (std::unique_ptr<brillo::UdevDevice> parent = device.GetParent(); parent;
       parent = parent->GetParent()) {
    if (ContainsRemovableAttribute(*parent)) {
      return true;
    }
  }
  return false;
}

}  // namespace brillo
