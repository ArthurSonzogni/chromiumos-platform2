// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_H_
#define RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_H_

namespace rmad {

class DeviceManagementClient {
 public:
  DeviceManagementClient() = default;
  virtual ~DeviceManagementClient() = default;

  virtual bool IsCcdBlocked() = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_H_
