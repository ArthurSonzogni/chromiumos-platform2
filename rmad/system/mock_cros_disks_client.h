// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_MOCK_CROS_DISKS_CLIENT_H_
#define RMAD_SYSTEM_MOCK_CROS_DISKS_CLIENT_H_

#include "rmad/system/cros_disks_client.h"

#include <string>
#include <vector>

#include <base/callback.h>
#include <gmock/gmock.h>

namespace rmad {

class MockCrosDisksClient : public CrosDisksClient {
 public:
  MockCrosDisksClient() = default;
  MockCrosDisksClient(const MockCrosDisksClient&) = delete;
  MockCrosDisksClient& operator=(const MockCrosDisksClient&) = delete;
  ~MockCrosDisksClient() override = default;

  MOCK_METHOD(bool, EnumerateDevices, (std::vector<std::string>*), (override));
  MOCK_METHOD(bool,
              GetDeviceProperties,
              (const std::string&, DeviceProperties*),
              (override));
  MOCK_METHOD(void,
              AddMountCompletedHandler,
              (base::RepeatingCallback<void(const MountEntry&)>),
              (override));
  MOCK_METHOD(void,
              Mount,
              (const std::string&,
               const std::string&,
               const std::vector<std::string>&),
              (override));
  MOCK_METHOD(bool,
              Unmount,
              (const std::string&, const std::vector<std::string>&, uint32_t*),
              (override));
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_MOCK_CROS_DISKS_CLIENT_H_
