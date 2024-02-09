// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/shill_proxy.h"

#include "update_engine/cros/dbus_connection.h"

using org::chromium::flimflam::ManagerProxy;
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ServiceProxy;
using org::chromium::flimflam::ServiceProxyInterface;

namespace chromeos_update_engine {

ShillProxy::ShillProxy()
    : bus_(DBusConnection::Get()->GetDBus()),
      manager_proxy_(new ManagerProxy(bus_)) {}

ManagerProxyInterface* ShillProxy::GetManagerProxy() {
  return manager_proxy_.get();
}

std::unique_ptr<ServiceProxyInterface> ShillProxy::GetServiceForPath(
    const dbus::ObjectPath& path) {
  DCHECK(bus_.get());
  return std::unique_ptr<ServiceProxyInterface>(new ServiceProxy(bus_, path));
}

}  // namespace chromeos_update_engine
