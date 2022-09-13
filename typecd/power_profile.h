// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_POWER_PROFILE_H_
#define TYPECD_POWER_PROFILE_H_

#include <base/files/file_path.h>

namespace typecd {

// This class represents a set of power source and sink capabilities supported
// by a Type-C peripheral. The Linux kernel Type-C subsystem groups these Power
// Delivery Objects (PDOs) together in a "usb_power_delivery" object; we can
// take that to represent a "power profile".
// Currently, only Partner PowerProfiles are supported.
//
// Why add a PowerProfile class instead of embedding the PDOs directly into the
// Peripheral class? This is because Ports can have more than 1 PowerProfile.
// So, it is beneficial to maintain a similar abstraction here.
//
// TODO(b/245608929): Add Port support for PowerProfile objects.
class PowerProfile {
 public:
  explicit PowerProfile(const base::FilePath& syspath);
  PowerProfile(const PowerProfile&) = delete;
  PowerProfile& operator=(const PowerProfile&) = delete;

 private:
  // Sysfs path used to access power delivery directory.
  base::FilePath syspath_;
};

}  // namespace typecd

#endif  // TYPECD_POWER_PROFILE_H_
