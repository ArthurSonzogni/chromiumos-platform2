// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/fake_shill_proxy.h"

#include <utility>

namespace federated {
namespace {
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ManagerProxyMock;
using org::chromium::flimflam::ServiceProxy;
using org::chromium::flimflam::ServiceProxyInterface;
}  // namespace

FakeShillProxy::FakeShillProxy()
    : shill_manager_proxy_mock_(new ManagerProxyMock()) {}

ManagerProxyMock* FakeShillProxy::GetShillManagerProxy() {
  return shill_manager_proxy_mock_.get();
}

std::unique_ptr<ServiceProxyInterface>
FakeShillProxy::GetShillServiceProxyForPath(const dbus::ObjectPath& path) {
  auto it = service_proxy_mocks_.find(path.value());
  CHECK(it != service_proxy_mocks_.end())
      << "No ServiceProxyMock set for " << path.value();
  std::unique_ptr<ServiceProxyInterface> result = std::move(it->second);
  service_proxy_mocks_.erase(it);
  return result;
}

void FakeShillProxy::SetServiceProxyForPath(
    const std::string& path,
    std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
        service_proxy) {
  service_proxy_mocks_[path] = std::move(service_proxy);
}

}  // namespace federated
