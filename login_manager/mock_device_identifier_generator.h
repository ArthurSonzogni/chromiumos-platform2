// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_DEVICE_IDENTIFIER_GENERATOR_H_
#define LOGIN_MANAGER_MOCK_DEVICE_IDENTIFIER_GENERATOR_H_

#include <gmock/gmock.h>

#include "login_manager/device_identifier_generator.h"

namespace login_manager {

class MockDeviceIdentifierGenerator : public DeviceIdentifierGenerator {
 public:
  MockDeviceIdentifierGenerator(SystemUtils* system_utils,
                                LoginMetrics* metrics)
      : DeviceIdentifierGenerator(system_utils, metrics) {}
  MockDeviceIdentifierGenerator(const MockDeviceIdentifierGenerator&) = delete;
  MockDeviceIdentifierGenerator& operator=(
      const MockDeviceIdentifierGenerator&) = delete;

  ~MockDeviceIdentifierGenerator() override {}

  MOCK_METHOD(void, RequestStateKeys, (StateKeyCallback), (override));
  MOCK_METHOD(void,
              RequestPsmDeviceActiveSecret,
              (PsmDeviceActiveSecretCallback),
              (override));
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_DEVICE_IDENTIFIER_GENERATOR_H_
