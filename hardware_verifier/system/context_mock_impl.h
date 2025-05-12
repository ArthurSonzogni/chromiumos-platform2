// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_SYSTEM_CONTEXT_MOCK_IMPL_H_
#define HARDWARE_VERIFIER_SYSTEM_CONTEXT_MOCK_IMPL_H_

#include <base/files/scoped_temp_dir.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <libcrossystem/crossystem.h>

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

  const base::FilePath& root_dir() override { return root_dir_; }

  // Interfaces to access fake/mock objects.
  brillo::FakeCrosConfig* fake_cros_config() { return &fake_cros_config_; }

  crossystem::Crossystem* fake_crossystem() { return &fake_crossystem_; }

 private:
  brillo::FakeCrosConfig fake_cros_config_;
  crossystem::Crossystem fake_crossystem_;

  // Used to create a temporary root directory.
  base::ScopedTempDir temp_dir_;
  base::FilePath root_dir_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_SYSTEM_CONTEXT_MOCK_IMPL_H_
