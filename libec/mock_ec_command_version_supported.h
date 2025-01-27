// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_MOCK_EC_COMMAND_VERSION_SUPPORTED_H_
#define LIBEC_MOCK_EC_COMMAND_VERSION_SUPPORTED_H_

#include <gmock/gmock.h>
#include <libec/ec_command_version_supported.h>
#include <libec/fingerprint/fp_mode.h>

namespace ec {

class MockEcCommandVersionSupported
    : public ec::EcCommandVersionSupportedInterface {
 public:
  MockEcCommandVersionSupported() = default;
  ~MockEcCommandVersionSupported() override = default;

  MOCK_METHOD(ec::EcCmdVersionSupportStatus,
              EcCmdVersionSupported,
              (uint16_t cmd, uint32_t ver),
              (override));
};

}  // namespace ec

#endif  // LIBEC_MOCK_EC_COMMAND_VERSION_SUPPORTED_H_
