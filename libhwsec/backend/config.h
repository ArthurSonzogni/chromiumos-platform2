// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_CONFIG_H_
#define LIBHWSEC_BACKEND_CONFIG_H_

#include <string>

#include <brillo/secure_blob.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

// Config provide the functions to change settings and policies.
class Config {
 public:
  struct QuoteResult {
    brillo::Blob unquoted_device_config;
    brillo::Blob quoted_data;
    brillo::Blob signature;
  };

  // Converts the operation |policy| setting to operation policy.
  virtual StatusOr<OperationPolicy> ToOperationPolicy(
      const OperationPolicySetting& policy) = 0;

  // Sets the |current_user| config.
  virtual Status SetCurrentUser(const std::string& current_user) = 0;

  // Is the current user had been set or not.
  virtual StatusOr<bool> IsCurrentUserSet() = 0;

  // Quotes the |device_config| with |key|.
  virtual StatusOr<QuoteResult> Quote(DeviceConfigs device_config, Key key) = 0;

 protected:
  Config() = default;
  ~Config() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_CONFIG_H_
