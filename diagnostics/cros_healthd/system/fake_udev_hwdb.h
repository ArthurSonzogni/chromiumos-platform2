// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_UDEV_HWDB_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_UDEV_HWDB_H_

#include <memory>
#include <string>
#include <vector>

#include "diagnostics/cros_healthd/system/udev_hwdb.h"

namespace diagnostics {

class FakeUdevHwdb : public UdevHwdb {
 public:
  FakeUdevHwdb() = default;
  FakeUdevHwdb(const FakeUdevHwdb& oth) = default;
  FakeUdevHwdb(FakeUdevHwdb&& oth) = default;
  ~FakeUdevHwdb() override = default;

  PropertieType GetProperties(const std::string& modalias) override;

  // If set to true, returns an empty properties map rather than a fake one.
  void SetReturnEmptyProperties(bool val) { return_empty_properties_ = val; }

 private:
  bool return_empty_properties_ = false;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_UDEV_HWDB_H_
