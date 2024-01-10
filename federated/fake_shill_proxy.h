// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FAKE_SHILL_PROXY_H_
#define FEDERATED_FAKE_SHILL_PROXY_H_

#include "federated/shill_proxy_interface.h"

#include <map>
#include <memory>
#include <string>

#include <shill/dbus-proxies.h>
#include <shill/dbus-proxy-mocks.h>

namespace federated {
class FakeShillProxy : public ShillProxyInterface {
 public:
  FakeShillProxy();
  FakeShillProxy(const FakeShillProxy&) = delete;
  FakeShillProxy& operator=(const FakeShillProxy&) = delete;
  ~FakeShillProxy() override = default;

  // Returns the subclass ManagerProxyMock of ManagerProxyInterface so that
  // unittest can easily use it.
  org::chromium::flimflam::ManagerProxyMock* GetShillManagerProxy() override;

  std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
  GetShillServiceProxyForPath(const dbus::ObjectPath& path) override;

  void SetServiceProxyForPath(
      const std::string& path,
      std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
          service_proxy);

 private:
  std::unique_ptr<org::chromium::flimflam::ManagerProxyMock>
      shill_manager_proxy_mock_;

  std::map<std::string,
           std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>>
      service_proxy_mocks_;
};

}  // namespace federated

#endif  // FEDERATED_FAKE_SHILL_PROXY_H_
