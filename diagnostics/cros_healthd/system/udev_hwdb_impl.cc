// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <libudev.h>

#include "diagnostics/cros_healthd/system/udev_hwdb_impl.h"

namespace diagnostics {

UdevHwdbImpl::UdevHwdbImpl() {
  udev_ = udev_new();
  CHECK(udev_);
  hwdb_ = udev_hwdb_new(udev_);
  CHECK(hwdb_);
}

UdevHwdbImpl::~UdevHwdbImpl() {
  CHECK(!udev_hwdb_unref(hwdb_));
  CHECK(!udev_unref(udev_));
}

UdevHwdbImpl::PropertieType UdevHwdbImpl::GetProperties(
    const std::string& modalias) {
  PropertieType res;
  struct udev_list_entry* entry;
  udev_list_entry_foreach(entry, udev_hwdb_get_properties_list_entry(
                                     hwdb_, modalias.c_str(), /*flags=*/0)) {
    res[udev_list_entry_get_name(entry)] = udev_list_entry_get_value(entry);
  }
  return res;
}

}  // namespace diagnostics
