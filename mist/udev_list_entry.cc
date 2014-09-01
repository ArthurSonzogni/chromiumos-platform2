// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mist/udev_list_entry.h"

#include <base/logging.h>

#include <libudev.h>

namespace mist {

UdevListEntry::UdevListEntry() : list_entry_(nullptr) {}

UdevListEntry::UdevListEntry(udev_list_entry* list_entry)
    : list_entry_(list_entry) {
  CHECK(list_entry_);
}

UdevListEntry* UdevListEntry::GetNext() const {
  udev_list_entry* list_entry = udev_list_entry_get_next(list_entry_);
  return list_entry ? new UdevListEntry(list_entry) : nullptr;
}

UdevListEntry* UdevListEntry::GetByName(const char* name) const {
  udev_list_entry* list_entry = udev_list_entry_get_by_name(list_entry_, name);
  return list_entry ? new UdevListEntry(list_entry) : nullptr;
}

const char* UdevListEntry::GetName() const {
  return udev_list_entry_get_name(list_entry_);
}

const char* UdevListEntry::GetValue() const {
  return udev_list_entry_get_value(list_entry_);
}

}  // namespace mist
