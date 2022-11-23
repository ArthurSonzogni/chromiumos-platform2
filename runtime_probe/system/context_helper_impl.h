// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_HELPER_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_HELPER_IMPL_H_

#include <memory>

#include <brillo/dbus/dbus_connection.h>
#include <shill/dbus-proxies.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

class ContextHelperImpl : public Context {
 public:
  ContextHelperImpl() = default;
  ~ContextHelperImpl() override = default;

  org::chromium::debugdProxyInterface* debugd_proxy() override {
    NOTREACHED() << "The helper should not call debugd.";
    return nullptr;
  };

  HelperInvoker* helper_invoker() override {
    NOTREACHED() << "The helper should not call helper.";
    return nullptr;
  }

  org::chromium::flimflam::ManagerProxyInterface* shill_manager_proxy()
      override {
    if (!shill_manager_proxy_) {
      SetupShillManagerProxy();
    }
    return shill_manager_proxy_.get();
  }

  std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface>
  CreateShillDeviceProxy(const dbus::ObjectPath& path) override;

 protected:
  // Setup the dbus connection.
  bool SetupDBusConnection();
  // Setup the shill manager proxy.
  void SetupShillManagerProxy();

  // The object to hold the dbus connection.
  brillo::DBusConnection connection_;
  // The reference of the dbus connection.
  scoped_refptr<dbus::Bus> dbus_bus_;
  // The proxy object for shill manager.
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      shill_manager_proxy_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_HELPER_IMPL_H_
