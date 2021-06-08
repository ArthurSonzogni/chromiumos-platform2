// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_INTERFACE_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_INTERFACE_H_

#include <memory>

#include "diagnostics/cros_healthd/system/pci_util.h"
#include "diagnostics/cros_healthd/system/udev_hwdb.h"

namespace diagnostics {

// The udev related interfaces.
class UdevInterface {
 public:
  virtual ~UdevInterface() = default;

  // Creates an object for accessing |PciUtil| interface.
  virtual std::unique_ptr<PciUtil> CreatePciUtil() = 0;
  // Creates an object for accessing |UdevHwdb| interface.
  virtual std::unique_ptr<UdevHwdb> CreateHwdb() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_INTERFACE_H_
