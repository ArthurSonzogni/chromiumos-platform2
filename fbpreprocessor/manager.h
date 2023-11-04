// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_MANAGER_H_
#define FBPREPROCESSOR_MANAGER_H_

#include <memory>

#include <dbus/bus.h>

#include "fbpreprocessor/configuration.h"
#include "fbpreprocessor/platform_features_client.h"

namespace fbpreprocessor {

class InputManager;
class OutputManager;
class PseudonymizationManager;
class SessionStateManager;

class Manager {
 public:
  explicit Manager(const Configuration& config);
  ~Manager();

  void Start(dbus::Bus* bus);

  // Is the user allowed to add firmware dumps to feedback reports? This will
  // return false if any condition (Finch, policy, allowlist, etc.) is not met.
  bool FirmwareDumpsAllowed();

  SessionStateManager* session_state_manager() const {
    return session_state_manager_.get();
  }

  PseudonymizationManager* pseudonymization_manager() const {
    return pseudonymization_manager_.get();
  }

  OutputManager* output_manager() const { return output_manager_.get(); }

  InputManager* input_manager() const { return input_manager_.get(); }

  PlatformFeaturesClient* platform_features() const {
    return platform_features_.get();
  }

  int default_file_expiration_in_secs() const {
    return default_file_expiration_in_secs_;
  }

  // Makes it possible to bypass the checks for policy, finch, etc. This must
  // only be used in unit tests to avoid having to fake the whole
  // policy/finch/.../ environment.
  void BypassAllowedForTests(bool bypass);

 private:
  int default_file_expiration_in_secs_;

  // If true, fbprocessord will process firmware dumps even if the
  // policy/finch/.../ settings would disable that. Only allowed for unit tests.
  bool bypass_allowed_for_tests_ = false;

  std::unique_ptr<PseudonymizationManager> pseudonymization_manager_;
  std::unique_ptr<OutputManager> output_manager_;
  std::unique_ptr<InputManager> input_manager_;
  std::unique_ptr<SessionStateManager> session_state_manager_;
  std::unique_ptr<PlatformFeaturesClient> platform_features_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_MANAGER_H_
