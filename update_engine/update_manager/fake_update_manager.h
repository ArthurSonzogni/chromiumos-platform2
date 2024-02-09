// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_UPDATE_MANAGER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_UPDATE_MANAGER_H_

#include "update_engine/update_manager/update_manager.h"

#include "update_engine/update_manager/fake_state.h"

namespace chromeos_update_manager {

class FakeUpdateManager : public UpdateManager {
 public:
  FakeUpdateManager()
      : UpdateManager(base::Seconds(5), base::Hours(1), new FakeState()) {}
  FakeUpdateManager(const FakeUpdateManager&) = delete;
  FakeUpdateManager& operator=(const FakeUpdateManager&) = delete;

  FakeState* state() {
    return reinterpret_cast<FakeState*>(UpdateManager::state());
  }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_UPDATE_MANAGER_H_
