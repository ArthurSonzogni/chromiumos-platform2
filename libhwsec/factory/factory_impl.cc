// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/factory/factory_impl.h"

#include <memory>
#include <utility>

#include "libhwsec/backend/backend.h"
#include "libhwsec/frontend/chaps/frontend_impl.h"
#include "libhwsec/frontend/client/frontend_impl.h"
#include "libhwsec/frontend/cryptohome/frontend_impl.h"
#include "libhwsec/frontend/pinweaver/frontend_impl.h"
#include "libhwsec/frontend/recovery_crypto/frontend_impl.h"
#include "libhwsec/frontend/u2fd/frontend_impl.h"
#include "libhwsec/middleware/middleware.h"

namespace hwsec {

FactoryImpl::FactoryImpl() {}

FactoryImpl::FactoryImpl(FactoryImpl::OnCurrentTaskRunner)
    : middleware_(MiddlewareOwner::OnCurrentTaskRunner()) {}

FactoryImpl::~FactoryImpl() {}

std::unique_ptr<CryptohomeFrontend> FactoryImpl::GetCryptohomeFrontend() {
  return std::make_unique<CryptohomeFrontendImpl>(
      Middleware(middleware_.Derive()));
}

std::unique_ptr<PinWeaverFrontend> FactoryImpl::GetPinWeaverFrontend() {
  return std::make_unique<PinWeaverFrontendImpl>(
      Middleware(middleware_.Derive()));
}

std::unique_ptr<RecoveryCryptoFrontend>
FactoryImpl::GetRecoveryCryptoFrontend() {
  return std::make_unique<RecoveryCryptoFrontendImpl>(
      Middleware(middleware_.Derive()));
}

std::unique_ptr<ClientFrontend> FactoryImpl::GetClientFrontend() {
  return std::make_unique<ClientFrontendImpl>(Middleware(middleware_.Derive()));
}

std::unique_ptr<ChapsFrontend> FactoryImpl::GetChapsFrontend() {
  return std::make_unique<ChapsFrontendImpl>(Middleware(middleware_.Derive()));
}

std::unique_ptr<U2fFrontend> FactoryImpl::GetU2fFrontend() {
  return std::make_unique<U2fFrontendImpl>(Middleware(middleware_.Derive()));
}

}  // namespace hwsec
