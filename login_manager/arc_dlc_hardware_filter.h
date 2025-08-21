// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_H_
#define LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_H_

namespace login_manager {

// An abstract interface for checking if a device satisfies the hardware
// requirements to install the ARCVM image from a DLC and allow ARC on the
// device.
class ArcDlcHardwareFilter {
 public:
  virtual ~ArcDlcHardwareFilter() = default;

  // Performs all hardware checks to determine if the device meets the minimum
  // requirements to enable ARC. Returns true if the device's hardware meets the
  // requirements to support the ARC DLC, otherwise returns false.
  virtual bool IsArcDlcHardwareRequirementSatisfied() const = 0;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_H_
