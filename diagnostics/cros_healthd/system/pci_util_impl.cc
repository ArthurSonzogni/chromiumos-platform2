// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>

#include "diagnostics/cros_healthd/system/pci_util_impl.h"

extern "C" {
#include <pci/pci.h>
}

namespace diagnostics {

namespace {
// These buffer sizes are referred from pciutils/lspci.c.
const int kVendorBufferSize = 128;
const int kDeviceBufferSize = 128;
}  // namespace

PciUtilImpl::PciUtilImpl() {
  pacc_ = pci_alloc();
  CHECK(pacc_);
  pci_init(pacc_);
}

PciUtilImpl::~PciUtilImpl() {
  pci_cleanup(pacc_);
}

std::string PciUtilImpl::GetVendorName(uint16_t vendor_id) {
  char buf[kVendorBufferSize];
  return pci_lookup_name(pacc_, buf, sizeof(buf), PCI_LOOKUP_VENDOR,
                         static_cast<int>(vendor_id));
}

std::string PciUtilImpl::GetDeviceName(uint16_t vendor_id, uint16_t device_id) {
  char buf[kDeviceBufferSize];
  return pci_lookup_name(pacc_, buf, sizeof(buf), PCI_LOOKUP_DEVICE,
                         static_cast<int>(vendor_id),
                         static_cast<int>(device_id));
}

}  // namespace diagnostics
