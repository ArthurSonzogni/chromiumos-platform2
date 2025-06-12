// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_SYSTEM_CONTEXT_MOCK_IMPL_H_
#define HARDWARE_VERIFIER_SYSTEM_CONTEXT_MOCK_IMPL_H_

#include <memory>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <libcrossystem/crossystem.h>
#include <libsegmentation/feature_management.h>
#include <libsegmentation/feature_management_interface.h>

#include "hardware_verifier/system/context.h"

namespace hardware_verifier {

class ContextMockImpl : public Context {
 public:
  ContextMockImpl();
  ~ContextMockImpl() override;

  brillo::CrosConfigInterface* cros_config() override {
    return &fake_cros_config_;
  }

  crossystem::Crossystem* crossystem() override { return &fake_crossystem_; }

  segmentation::FeatureManagement* feature_management() override {
    return &fake_feature_management_;
  }

  const base::FilePath& root_dir() override { return root_dir_; }

  // Interfaces to access fake/mock objects.
  brillo::FakeCrosConfig* fake_cros_config() { return &fake_cros_config_; }

  crossystem::Crossystem* fake_crossystem() { return &fake_crossystem_; }

  void InitializeFeatureManagementForTest(
      std::unique_ptr<segmentation::FeatureManagementInterface> impl) {
    fake_feature_management_ = segmentation::FeatureManagement(std::move(impl));
  }

  base::ScopedTempDir* temp_dir() { return &temp_dir_; }

 private:
  brillo::FakeCrosConfig fake_cros_config_;
  crossystem::Crossystem fake_crossystem_;
  segmentation::FeatureManagement fake_feature_management_;

  // Used to create a temporary root directory.
  base::ScopedTempDir temp_dir_;
  base::FilePath root_dir_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_SYSTEM_CONTEXT_MOCK_IMPL_H_
