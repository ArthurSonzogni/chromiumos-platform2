// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "diagnostics/cros_healthd/system/fake_udev.h"

namespace diagnostics {

std::unique_ptr<PciUtil> FakeUdev::CreatePciUtil() {
  return std::unique_ptr<PciUtil>(new FakePciUtil(fake_pci_util_));
}

std::unique_ptr<UdevHwdb> FakeUdev::CreateHwdb() {
  return std::unique_ptr<UdevHwdb>(new FakeUdevHwdb(fake_udev_hwdb_));
}

}  // namespace diagnostics
