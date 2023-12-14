// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_DBUS_CONNECTOR_H_
#define HEARTD_DAEMON_DBUS_CONNECTOR_H_

namespace org {
namespace chromium {

class PowerManagerProxyInterface;

}  // namespace chromium
}  // namespace org

namespace heartd {

class DbusConnector {
 public:
  virtual ~DbusConnector() = default;

  // Use the object returned by power_manager_proxy() to communicate with power
  // manager daemon through dbus.
  virtual org::chromium::PowerManagerProxyInterface* power_manager_proxy() = 0;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_DBUS_CONNECTOR_H_
