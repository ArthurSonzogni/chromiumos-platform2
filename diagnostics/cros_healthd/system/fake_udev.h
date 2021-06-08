// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_UDEV_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_UDEV_H_

#include <memory>

#include "diagnostics/cros_healthd/system/fake_pci_util.h"
#include "diagnostics/cros_healthd/system/fake_udev_hwdb.h"
#include "diagnostics/cros_healthd/system/udev_interface.h"

namespace diagnostics {

class FakeUdev : public UdevInterface {
 public:
  FakeUdev() = default;
  FakeUdev(const FakeUdev& oth) = delete;
  FakeUdev(FakeUdev&& oth) = delete;
  ~FakeUdev() override = default;

  std::unique_ptr<PciUtil> CreatePciUtil() override;
  std::unique_ptr<UdevHwdb> CreateHwdb() override;
  FakePciUtil* fake_pci_util() { return &fake_pci_util_; }
  FakeUdevHwdb* fake_udev_hwdb() { return &fake_udev_hwdb_; }

 private:
  FakePciUtil fake_pci_util_;
  FakeUdevHwdb fake_udev_hwdb_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_UDEV_H_
