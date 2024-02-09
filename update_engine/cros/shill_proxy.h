// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_SHILL_PROXY_H_
#define UPDATE_ENGINE_CROS_SHILL_PROXY_H_

#include <memory>
#include <string>

#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <shill/dbus-proxies.h>

#include "update_engine/cros/shill_proxy_interface.h"

namespace chromeos_update_engine {

// This class implements the connection to shill using real DBus calls.
class ShillProxy : public ShillProxyInterface {
 public:
  ShillProxy();
  ShillProxy(const ShillProxy&) = delete;
  ShillProxy& operator=(const ShillProxy&) = delete;

  ~ShillProxy() override = default;

  // ShillProxyInterface overrides.
  org::chromium::flimflam::ManagerProxyInterface* GetManagerProxy() override;
  std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
  GetServiceForPath(const dbus::ObjectPath& path) override;

 private:
  // A reference to the main bus for creating new ServiceProxy instances.
  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      manager_proxy_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_SHILL_PROXY_H_
