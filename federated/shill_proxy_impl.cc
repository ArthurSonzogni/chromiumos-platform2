// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/shill_proxy_impl.h"

#include <dbus/bus.h>
#include <shill/dbus-proxies.h>

namespace federated {
namespace {
using org::chromium::flimflam::ManagerProxy;
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ServiceProxy;
using org::chromium::flimflam::ServiceProxyInterface;

}  // namespace

ShillProxyImpl::ShillProxyImpl(dbus::Bus* bus)
    : bus_(bus),
      shill_manager_proxy_(new org::chromium::flimflam::ManagerProxy(bus)) {}

ManagerProxyInterface* ShillProxyImpl::GetShillManagerProxy() {
  return shill_manager_proxy_.get();
}

std::unique_ptr<ServiceProxyInterface>
ShillProxyImpl::GetShillServiceProxyForPath(const dbus::ObjectPath& path) {
  return std::make_unique<ServiceProxy>(bus_, path);
}

}  // namespace federated
