// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_IMPL_H_

#include <memory>

#include <base/check.h>
#include <brillo/dbus/dbus_connection.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

class ContextImpl : public Context {
 public:
  ~ContextImpl() override;

  org::chromium::debugdProxyInterface* debugd_proxy() override {
    CHECK(debugd_proxy_);
    return debugd_proxy_.get();
  };

  HelperInvoker* helper_invoker() override {
    CHECK(helper_invoker_);
    return helper_invoker_.get();
  }

 protected:
  // This interface should be used through its derived classes.
  ContextImpl();

  // Setups the dbus connection and the dbus services.
  bool SetupDBusServices();

  // The object to hold the dbus connection.
  brillo::DBusConnection connection_;
  // The reference of the dbus connection.
  scoped_refptr<dbus::Bus> dbus_bus_;
  // The proxy object for dbugd dbus service.
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  // The object for invoking helper.
  std::unique_ptr<HelperInvoker> helper_invoker_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_IMPL_H_
