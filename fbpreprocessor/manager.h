// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_MANAGER_H_
#define FBPREPROCESSOR_MANAGER_H_

#include <memory>

#include <dbus/bus.h>

namespace fbpreprocessor {

class OutputManager;
class PseudonymizationManager;
class SessionStateManager;

class Manager {
 public:
  explicit Manager(dbus::Bus* bus);
  ~Manager();

  SessionStateManager* session_state_manager() const {
    return session_state_manager_.get();
  }

  PseudonymizationManager* pseudonymization_manager() const {
    return pseudonymization_manager_.get();
  }

  OutputManager* output_manager() const { return output_manager_.get(); }

 private:
  scoped_refptr<dbus::Bus> bus_;

  std::unique_ptr<PseudonymizationManager> pseudonymization_manager_;
  std::unique_ptr<SessionStateManager> session_state_manager_;
  std::unique_ptr<OutputManager> output_manager_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_MANAGER_H_
