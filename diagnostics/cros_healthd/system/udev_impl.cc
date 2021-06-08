// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/check.h>
#include <libudev.h>

#include "diagnostics/cros_healthd/system/pci_util_impl.h"
#include "diagnostics/cros_healthd/system/udev_hwdb_impl.h"
#include "diagnostics/cros_healthd/system/udev_impl.h"

namespace diagnostics {

std::unique_ptr<PciUtil> UdevImpl::CreatePciUtil() {
  return std::unique_ptr<PciUtil>(new PciUtilImpl());
}

std::unique_ptr<UdevHwdb> UdevImpl::CreateHwdb() {
  return std::unique_ptr<UdevHwdb>(new UdevHwdbImpl());
}

}  // namespace diagnostics
