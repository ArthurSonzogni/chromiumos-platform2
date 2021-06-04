// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_HWDB_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_HWDB_IMPL_H_

#include <string>

#include <libudev.h>

#include "diagnostics/cros_healthd/system/udev_hwdb.h"

namespace diagnostics {

class UdevHwdbImpl : public UdevHwdb {
 public:
  UdevHwdbImpl();
  UdevHwdbImpl(const UdevHwdbImpl& oth) = delete;
  UdevHwdbImpl(UdevHwdbImpl&& oth) = delete;
  ~UdevHwdbImpl() override;

  PropertieType GetProperties(const std::string& modalias) override;

 private:
  struct udev* udev_;
  struct udev_hwdb* hwdb_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_HWDB_IMPL_H_
