// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_SHILL_PROXY_INTERFACE_H_
#define FEDERATED_SHILL_PROXY_INTERFACE_H_

#include <memory>

#include <dbus/object_path.h>
#include <shill/dbus-proxies.h>

namespace federated {

// The interface that handles DBus connection to shill used by
// network_status_training_condition. This class provides a mockable way for the
// unittests.
class ShillProxyInterface {
 public:
  ShillProxyInterface(const ShillProxyInterface&) = delete;
  ShillProxyInterface& operator=(const ShillProxyInterface&) = delete;
  virtual ~ShillProxyInterface() = default;

  // Return the shill ManagerProxy of the shill daemon. The instance is owned by
  // this ShillProxyInterface instance.
  virtual org::chromium::flimflam::ManagerProxyInterface*
  GetShillManagerProxy() = 0;

  // Return the shill ServiceProxy of the shill daemon. The ownership of the
  // returned instance is transferred to the caller.
  virtual std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
  GetShillServiceProxyForPath(const dbus::ObjectPath& path) = 0;

 protected:
  ShillProxyInterface() = default;
};

}  // namespace federated

#endif  // FEDERATED_SHILL_PROXY_INTERFACE_H_
