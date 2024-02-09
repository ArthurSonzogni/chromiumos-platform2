// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_MOCK_CONNECTION_MANAGER_H_
#define UPDATE_ENGINE_CROS_MOCK_CONNECTION_MANAGER_H_

#include <gmock/gmock.h>

#include "update_engine/cros/connection_manager_interface.h"

namespace chromeos_update_engine {

// This class mocks the generic interface to the connection manager
// (e.g FlimFlam, Shill, etc.) to consolidate all connection-related
// logic in update_engine.
class MockConnectionManager : public ConnectionManagerInterface {
 public:
  MockConnectionManager() = default;

  MOCK_METHOD(bool,
              GetConnectionProperties,
              (ConnectionType * out_type, bool* out_metered));

  MOCK_CONST_METHOD0(IsUpdateAllowedOverMetered, bool());
  MOCK_CONST_METHOD0(IsAllowedConnectionTypesForUpdateSet, bool());
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_MOCK_CONNECTION_MANAGER_H_
