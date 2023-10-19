// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_FWMP_MOCK_FIRMWARE_MANAGEMENT_PARAMETERS_H_
#define DEVICE_MANAGEMENT_FWMP_MOCK_FIRMWARE_MANAGEMENT_PARAMETERS_H_

#include "device_management/fwmp/firmware_management_parameters.h"
#include <gmock/gmock.h>

namespace device_management {
class MockFirmwareManagementParameters
    : public fwmp::FirmwareManagementParameters {
 public:
  MockFirmwareManagementParameters() = default;
  virtual ~MockFirmwareManagementParameters() = default;

  MOCK_METHOD(bool, Create, (), (override));
  MOCK_METHOD(bool, Destroy, (), (override));
  MOCK_METHOD(bool, Load, (), (override));
  MOCK_METHOD(bool, Store, (uint32_t, const brillo::Blob*), (override));
  MOCK_METHOD(bool, GetFlags, (uint32_t*), (override));
  MOCK_METHOD(bool, GetDeveloperKeyHash, (brillo::Blob*), (override));
  MOCK_METHOD(bool, IsLoaded, (), (const, override));
};
}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_FWMP_MOCK_FIRMWARE_MANAGEMENT_PARAMETERS_H_
