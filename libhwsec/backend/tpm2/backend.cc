// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "libhwsec/backend/tpm2/backend.h"

namespace hwsec {

BackendTpm2::BackendTpm2(Proxy& proxy,
                         MiddlewareDerivative middleware_derivative)
    : proxy_(proxy),
      trunks_context_({
          .command_transceiver = proxy_.GetTrunksCommandTransceiver(),
          .factory = proxy_.GetTrunksFactory(),
          .tpm_state = proxy_.GetTrunksFactory().GetTpmState(),
          .tpm_utility = proxy_.GetTrunksFactory().GetTpmUtility(),
      }),
      middleware_derivative_(middleware_derivative) {}

BackendTpm2::~BackendTpm2() {}

}  // namespace hwsec
