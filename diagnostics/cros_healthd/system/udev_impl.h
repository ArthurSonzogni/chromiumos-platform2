// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_IMPL_H_

#include <memory>

#include "diagnostics/cros_healthd/system/udev_interface.h"

namespace diagnostics {

class UdevImpl : public UdevInterface {
 public:
  UdevImpl() = default;
  UdevImpl(const UdevImpl& oth) = delete;
  UdevImpl(UdevImpl&& oth) = delete;
  ~UdevImpl() override = default;

  std::unique_ptr<PciUtil> CreatePciUtil() override;
  std::unique_ptr<UdevHwdb> CreateHwdb() override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_IMPL_H_
