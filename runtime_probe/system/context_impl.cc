// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/logging.h>
#include <debugd/dbus-proxies.h>

#include "runtime_probe/system/context_impl.h"

namespace runtime_probe {
bool ContextImpl::SetupDBusServices() {
  dbus_bus_ = connection_.Connect();
  if (!dbus_bus_) {
    LOG(ERROR) << "Cannot connect to dbus.";
    return false;
  }
  debugd_proxy_ = std::make_unique<org::chromium::debugdProxy>(dbus_bus_);
  return true;
}

}  // namespace runtime_probe
