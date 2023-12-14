// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_TEST_UTILS_MOCK_DBUS_CONNECTOR_H_
#define HEARTD_DAEMON_TEST_UTILS_MOCK_DBUS_CONNECTOR_H_

#include <memory>

#include <power_manager/dbus-proxy-mocks.h>

#include "heartd/daemon/dbus_connector.h"

namespace heartd {

class MockDbusConnector final : public DbusConnector {
 public:
  MockDbusConnector();
  MockDbusConnector(const MockDbusConnector&) = delete;
  MockDbusConnector& operator=(const MockDbusConnector&) = delete;
  ~MockDbusConnector();

  org::chromium::PowerManagerProxyMock* power_manager_proxy() override;

 private:
  std::unique_ptr<org::chromium::PowerManagerProxyMock> power_manager_proxy_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_TEST_UTILS_MOCK_DBUS_CONNECTOR_H_
