// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_TETHERING_MANAGER_H_
#define SHILL_MOCK_TETHERING_MANAGER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "shill/tethering_manager.h"

namespace shill {

class MockTetheringManager : public TetheringManager {
 public:
  explicit MockTetheringManager(Manager* manager) : TetheringManager(manager) {}
  MockTetheringManager(const MockTetheringManager&) = delete;
  MockTetheringManager& operator=(const MockTetheringManager&) = delete;

  ~MockTetheringManager() override = default;

  MOCK_METHOD(void, OnDeviceCreated, (HotspotDeviceRefPtr), (override));
  MOCK_METHOD(void, OnDeviceCreationFailed, (), (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_TETHERING_MANAGER_H_
