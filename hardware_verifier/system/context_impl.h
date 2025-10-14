// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_SYSTEM_CONTEXT_IMPL_H_
#define HARDWARE_VERIFIER_SYSTEM_CONTEXT_IMPL_H_

#include <chromeos-config/libcros_config/cros_config.h>
#include <chromeos/hardware_verifier/runtime_hwid_utils/runtime_hwid_utils.h>
#include <chromeos/hardware_verifier/runtime_hwid_utils/runtime_hwid_utils_impl.h>
#include <libcrossystem/crossystem.h>
#include <libsegmentation/feature_management.h>

#include "hardware_verifier/system/context.h"

namespace hardware_verifier {

class ContextImpl : public Context {
 public:
  ContextImpl();
  ~ContextImpl() override = default;

  brillo::CrosConfigInterface* cros_config() override;

  crossystem::Crossystem* crossystem() override { return &crossystem_; }

  segmentation::FeatureManagement* feature_management() override {
    return &feature_management_;
  }

  RuntimeHWIDUtils* runtime_hwid_utils() override {
    return &runtime_hwid_utils_;
  }

 private:
  // The object to access the ChromeOS model configuration.
  brillo::CrosConfig cros_config_;

  // The object to access crossystem system properties.
  crossystem::Crossystem crossystem_;

  // The object to access feature_management system properties.
  segmentation::FeatureManagement feature_management_;

  // The object to access Runtime HWID.
  RuntimeHWIDUtilsImpl runtime_hwid_utils_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_SYSTEM_CONTEXT_IMPL_H_
