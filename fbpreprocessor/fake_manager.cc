// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/fake_manager.h"

#include <memory>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/bus.h>

#include "fbpreprocessor/fake_session_state_manager.h"
#include "fbpreprocessor/storage.h"

namespace {
constexpr int kTestDefaultExpirationSeconds = 1800;
}  // namespace

namespace fbpreprocessor {

FakeManager::FakeManager()
    : fw_dumps_allowed_(true),
      default_file_expiration_in_secs_(kTestDefaultExpirationSeconds) {
  session_state_manager_ = std::make_unique<FakeSessionStateManager>(this);
}

void FakeManager::Start(dbus::Bus* bus) {
  SetupFakeDaemonStore();
}

SessionStateManagerInterface* FakeManager::session_state_manager() const {
  return session_state_manager_.get();
}

void FakeManager::SimulateUserLogin() {
  session_state_manager_->SimulateLogin();
}

void FakeManager::SimulateUserLogout() {
  session_state_manager_->SimulateLogout();
}

void FakeManager::SetupFakeDaemonStore() {
  CHECK(root_dir_.CreateUniqueTempDir());
  CHECK(base::CreateDirectory(
      root_dir_.GetPath().Append(kTestUserHash).Append(kInputDirectory)));
  CHECK(base::CreateDirectory(
      root_dir_.GetPath().Append(kTestUserHash).Append(kProcessedDirectory)));
}

}  // namespace fbpreprocessor
