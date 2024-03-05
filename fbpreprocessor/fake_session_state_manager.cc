// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/fake_session_state_manager.h"

#include "fbpreprocessor/fake_manager.h"
#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

FakeSessionStateManager::FakeSessionStateManager(Manager* manager)
    : manager_(manager) {}

void FakeSessionStateManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}
void FakeSessionStateManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FakeSessionStateManager::SimulateLogin() {
  for (auto& observer : observers_) {
    observer.OnUserLoggedIn(FakeManager::kTestUserHash.data());
  }
}

void FakeSessionStateManager::SimulateLogout() {
  for (auto& observer : observers_) {
    observer.OnUserLoggedOut();
  }
}

}  // namespace fbpreprocessor
