// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/fake_manager.h"

#include <dbus/bus.h>

namespace {
constexpr int kTestDefaultExpirationSeconds = 1800;
}  // namespace

namespace fbpreprocessor {

FakeManager::FakeManager()
    : fw_dumps_allowed_(true),
      default_file_expiration_in_secs_(kTestDefaultExpirationSeconds) {}

void FakeManager::Start(dbus::Bus* bus) {}

}  // namespace fbpreprocessor
