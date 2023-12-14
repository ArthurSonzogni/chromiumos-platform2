// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_DBUS_CONNECTOR_IMPL_H_
#define HEARTD_DAEMON_DBUS_CONNECTOR_IMPL_H_

#include <memory>

#include <brillo/dbus/dbus_connection.h>

#include "heartd/daemon/dbus_connector.h"

namespace heartd {

class DbusConnectorImpl final : public DbusConnector {
 public:
  DbusConnectorImpl();
  DbusConnectorImpl(const DbusConnectorImpl&) = delete;
  DbusConnectorImpl& operator=(const DbusConnectorImpl&) = delete;
  ~DbusConnectorImpl() override;

  // Use the object returned by power_manager_proxy() to communicate with power
  // manager daemon through dbus.
  org::chromium::PowerManagerProxyInterface* power_manager_proxy() override;

 private:
  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus|.
  brillo::DBusConnection connection_;
  // Members accessed via the accessor functions defined above.
  std::unique_ptr<org::chromium::PowerManagerProxyInterface>
      power_manager_proxy_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_DBUS_CONNECTOR_IMPL_H_
