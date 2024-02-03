// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_MANAGER_IMPL_H_
#define FBPREPROCESSOR_MANAGER_IMPL_H_

#include <memory>

#include <base/task/sequenced_task_runner.h>
#include <dbus/bus.h>

#include "fbpreprocessor/configuration.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/platform_features_client.h"

namespace fbpreprocessor {

class InputManager;
class OutputManager;
class PseudonymizationManager;
class SessionStateManager;
class SessionStateManagerInterface;

class ManagerImpl : public Manager {
 public:
  explicit ManagerImpl(const Configuration& config);
  ~ManagerImpl();

  void Start(dbus::Bus* bus) override;

  // Is the user allowed to add firmware dumps to feedback reports? This will
  // return false if any condition (Finch, policy, allowlist, etc.) is not met.
  bool FirmwareDumpsAllowed() const override;

  SessionStateManagerInterface* session_state_manager() const override;

  PseudonymizationManager* pseudonymization_manager() const override {
    return pseudonymization_manager_.get();
  }

  OutputManager* output_manager() const override {
    return output_manager_.get();
  }

  InputManager* input_manager() const override { return input_manager_.get(); }

  PlatformFeaturesClient* platform_features() const override {
    return platform_features_.get();
  }

  scoped_refptr<base::SequencedTaskRunner> task_runner() const override {
    return task_runner_;
  }

  int default_file_expiration_in_secs() const override {
    return default_file_expiration_in_secs_;
  }

 private:
  int default_file_expiration_in_secs_;

  std::unique_ptr<PseudonymizationManager> pseudonymization_manager_;
  std::unique_ptr<OutputManager> output_manager_;
  std::unique_ptr<InputManager> input_manager_;
  std::unique_ptr<SessionStateManager> session_state_manager_;
  std::unique_ptr<PlatformFeaturesClient> platform_features_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_MANAGER_IMPL_H_
