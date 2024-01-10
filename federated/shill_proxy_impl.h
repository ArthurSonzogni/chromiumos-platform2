// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_SHILL_PROXY_IMPL_H_
#define FEDERATED_SHILL_PROXY_IMPL_H_

#include "federated/shill_proxy_interface.h"

#include <memory>

namespace dbus {
class Bus;
}

namespace federated {

class ShillProxyImpl : public ShillProxyInterface {
 public:
  explicit ShillProxyImpl(dbus::Bus* bus);
  ShillProxyImpl(const ShillProxyImpl&) = delete;
  ShillProxyImpl& operator=(const ShillProxyImpl&) = delete;

  ~ShillProxyImpl() override = default;

  org::chromium::flimflam::ManagerProxyInterface* GetShillManagerProxy()
      override;

  std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
  GetShillServiceProxyForPath(const dbus::ObjectPath& path) override;

 private:
  // Not owned:
  dbus::Bus* bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      shill_manager_proxy_;
};

}  // namespace federated
#endif  // FEDERATED_SHILL_PROXY_IMPL_H_
