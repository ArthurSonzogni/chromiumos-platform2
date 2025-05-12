// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/system/context_impl.h"

#include <chromeos-config/libcros_config/cros_config.h>

namespace hardware_verifier {
ContextImpl::ContextImpl() = default;

brillo::CrosConfigInterface* ContextImpl::cros_config() {
  return &cros_config_;
}

}  // namespace hardware_verifier
