// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/test_utils/mock_dbus_connector.h"

#include <memory>

#include <power_manager/dbus-proxy-mocks.h>

namespace heartd {

MockDbusConnector::MockDbusConnector() {
  power_manager_proxy_ =
      std::make_unique<org::chromium::PowerManagerProxyMock>();
}

MockDbusConnector::~MockDbusConnector() = default;

org::chromium::PowerManagerProxyMock* MockDbusConnector::power_manager_proxy() {
  return power_manager_proxy_.get();
}

}  // namespace heartd
