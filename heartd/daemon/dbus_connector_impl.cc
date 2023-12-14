// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/dbus_connector_impl.h"

#include <memory>

#include <power_manager/dbus-proxies.h>

namespace heartd {

DbusConnectorImpl::DbusConnectorImpl() {
  auto dbus_bus = connection_.Connect();
  CHECK(dbus_bus) << "Failed to connect to the D-Bus system bus.";
  power_manager_proxy_ =
      std::make_unique<org::chromium::PowerManagerProxy>(dbus_bus);
}

DbusConnectorImpl::~DbusConnectorImpl() = default;

org::chromium::PowerManagerProxyInterface*
DbusConnectorImpl::power_manager_proxy() {
  return power_manager_proxy_.get();
}

}  // namespace heartd
