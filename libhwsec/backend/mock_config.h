// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_CONFIG_H_
#define LIBHWSEC_BACKEND_MOCK_CONFIG_H_

#include <string>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/config.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class MockConfig : public Config {
 public:
  MOCK_METHOD(StatusOr<OperationPolicy>,
              ToOperationPolicy,
              (const OperationPolicySetting& policy),
              (override));
  MOCK_METHOD(Status,
              SetCurrentUser,
              (const std::string& current_user),
              (override));
  MOCK_METHOD(StatusOr<bool>, IsCurrentUserSet, (), (override));
  MOCK_METHOD(StatusOr<QuoteResult>,
              Quote,
              (DeviceConfigs device_config, Key key),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_CONFIG_H_
