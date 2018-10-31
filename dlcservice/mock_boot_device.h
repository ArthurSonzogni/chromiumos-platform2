// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_MOCK_BOOT_DEVICE_H_
#define DLCSERVICE_MOCK_BOOT_DEVICE_H_

#include "dlcservice/boot_device.h"

#include <string>

#include <base/macros.h>

namespace dlcservice {

class MockBootDevice : public BootDeviceInterface {
 public:
  MockBootDevice() = default;

  MOCK_METHOD1(IsRemovableDevice, bool(const std::string&));
  MOCK_METHOD0(GetBootDevice, std::string());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockBootDevice);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_MOCK_BOOT_DEVICE_H_
