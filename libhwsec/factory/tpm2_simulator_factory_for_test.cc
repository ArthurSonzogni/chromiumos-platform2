// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/factory/tpm2_simulator_factory_for_test.h"

#include <memory>
#include <utility>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/tpm2_simulator_proxy_for_test.h"

namespace hwsec {
namespace {

std::unique_ptr<Proxy> GetAndInitProxy() {
  auto proxy = std::make_unique<Tpm2SimulatorProxyForTest>();
  CHECK(proxy->Init());
  return proxy;
}

std::unique_ptr<MiddlewareOwner> GetAndInitMiddlewareOwner(ThreadingMode mode,
                                                           Proxy& proxy) {
  auto backend = std::make_unique<BackendTpm2>(proxy, MiddlewareDerivative{});
  BackendTpm2* backend_ptr = backend.get();
  auto middleware = std::make_unique<MiddlewareOwner>(std::move(backend), mode);
  backend_ptr->set_middleware_derivative_for_test(middleware->Derive());
  return middleware;
}

}  // namespace

Tpm2SimulatorFactoryForTestProxy::Tpm2SimulatorFactoryForTestProxy(
    std::unique_ptr<Proxy> proxy)
    : proxy_(std::move(proxy)) {}

Tpm2SimulatorFactoryForTestProxy::~Tpm2SimulatorFactoryForTestProxy() = default;

Tpm2SimulatorFactoryForTest::Tpm2SimulatorFactoryForTest(ThreadingMode mode)
    : Tpm2SimulatorFactoryForTestProxy(GetAndInitProxy()),
      FactoryImpl(GetAndInitMiddlewareOwner(mode, *proxy_)) {}

Tpm2SimulatorFactoryForTest::~Tpm2SimulatorFactoryForTest() = default;

}  // namespace hwsec
