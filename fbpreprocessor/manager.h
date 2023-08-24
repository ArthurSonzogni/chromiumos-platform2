// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_MANAGER_H_
#define FBPREPROCESSOR_MANAGER_H_

#include <memory>

#include <dbus/bus.h>

#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

class Manager {
 public:
  explicit Manager(dbus::Bus* bus);
  ~Manager() = default;

  SessionStateManager* session_state_manager() const {
    return session_state_manager_.get();
  }

 private:
  scoped_refptr<dbus::Bus> bus_;

  std::unique_ptr<SessionStateManager> session_state_manager_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_MANAGER_H_
