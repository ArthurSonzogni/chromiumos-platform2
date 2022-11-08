// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/factory/tpm2_simulator_factory_for_test.h"

#include <memory>
#include <utility>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/frontend/chaps/frontend_impl.h"
#include "libhwsec/frontend/client/frontend_impl.h"
#include "libhwsec/frontend/cryptohome/frontend_impl.h"
#include "libhwsec/frontend/pinweaver/frontend_impl.h"
#include "libhwsec/frontend/recovery_crypto/frontend_impl.h"
#include "libhwsec/frontend/u2fd/frontend_impl.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/tpm2_simulator_proxy_for_test.h"

namespace hwsec {

Tpm2SimulatorFactoryForTest::Tpm2SimulatorFactoryForTest() {
  auto proxy = std::make_unique<Tpm2SimulatorProxyForTest>();
  CHECK(proxy->Init());
  proxy_ = std::move(proxy);

  auto backend = std::make_unique<BackendTpm2>(*proxy_, MiddlewareDerivative{});
  BackendTpm2* backend_ptr = backend.get();
  backend_ = std::move(backend);

  middleware_ = std::make_unique<MiddlewareOwner>(
      std::move(backend_), MiddlewareOwner::OnCurrentTaskRunner{});

  backend_ptr->set_middleware_derivative_for_test(middleware_->Derive());
}

Tpm2SimulatorFactoryForTest::~Tpm2SimulatorFactoryForTest() {}

std::unique_ptr<CryptohomeFrontend>
Tpm2SimulatorFactoryForTest::GetCryptohomeFrontend() {
  return std::make_unique<CryptohomeFrontendImpl>(middleware_->Derive());
}

std::unique_ptr<PinWeaverFrontend>
Tpm2SimulatorFactoryForTest::GetPinWeaverFrontend() {
  return std::make_unique<PinWeaverFrontendImpl>(middleware_->Derive());
}

std::unique_ptr<RecoveryCryptoFrontend>
Tpm2SimulatorFactoryForTest::GetRecoveryCryptoFrontend() {
  return std::make_unique<RecoveryCryptoFrontendImpl>(middleware_->Derive());
}

std::unique_ptr<ClientFrontend>
Tpm2SimulatorFactoryForTest::GetClientFrontend() {
  return std::make_unique<ClientFrontendImpl>(middleware_->Derive());
}

std::unique_ptr<ChapsFrontend> Tpm2SimulatorFactoryForTest::GetChapsFrontend() {
  return std::make_unique<ChapsFrontendImpl>(middleware_->Derive());
}

std::unique_ptr<U2fFrontend> Tpm2SimulatorFactoryForTest::GetU2fFrontend() {
  return std::make_unique<U2fFrontendImpl>(middleware_->Derive());
}

}  // namespace hwsec
