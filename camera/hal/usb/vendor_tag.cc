/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/vendor_tag.h"

namespace cros {

// static
const VendorTagManager& VendorTagOps::GetVendorTagManager() {
  static const auto manager = []() {
    VendorTagManager m;
    m.Add(kVendorTagVendorId, kVendorUsbSectionName, "vendorId", TYPE_BYTE);
    m.Add(kVendorTagProductId, kVendorUsbSectionName, "productId", TYPE_BYTE);
    m.Add(kVendorTagModelName, kVendorUsbSectionName, "modelName", TYPE_BYTE);
    return m;
  }();
  return manager;
}

// static
int VendorTagOps::GetTagCount(const vendor_tag_ops_t*) {
  return GetVendorTagManager().GetTagCount();
}

// static
void VendorTagOps::GetAllTags(const vendor_tag_ops_t*, uint32_t* tag_array) {
  GetVendorTagManager().GetAllTags(tag_array);
}

// static
const char* VendorTagOps::GetSectionName(const vendor_tag_ops_t*,
                                         uint32_t tag) {
  return GetVendorTagManager().GetSectionName(tag);
}

// static
const char* VendorTagOps::GetTagName(const vendor_tag_ops_t*, uint32_t tag) {
  return GetVendorTagManager().GetTagName(tag);
}

// static
int VendorTagOps::GetTagType(const vendor_tag_ops_t*, uint32_t tag) {
  return GetVendorTagManager().GetTagType(tag);
}

}  // namespace cros
