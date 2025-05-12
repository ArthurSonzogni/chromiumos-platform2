// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_SYSTEM_CONTEXT_IMPL_H_
#define HARDWARE_VERIFIER_SYSTEM_CONTEXT_IMPL_H_

#include <chromeos-config/libcros_config/cros_config.h>
#include <libcrossystem/crossystem.h>

#include "hardware_verifier/system/context.h"

namespace hardware_verifier {

class ContextImpl : public Context {
 public:
  ContextImpl();
  ~ContextImpl() override = default;

  brillo::CrosConfigInterface* cros_config() override;

  crossystem::Crossystem* crossystem() override { return &crossystem_; }

 private:
  // The object to access the ChromeOS model configuration.
  brillo::CrosConfig cros_config_;

  // The object to access crossystem system properties.
  crossystem::Crossystem crossystem_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_SYSTEM_CONTEXT_IMPL_H_
