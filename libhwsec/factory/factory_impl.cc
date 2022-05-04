// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/factory/factory_impl.h"

#include <memory>
#include <utility>

#include "libhwsec/backend/backend.h"
#include "libhwsec/frontend/cryptohome/frontend_impl.h"
#include "libhwsec/middleware/middleware.h"

namespace hwsec {

FactoryImpl::FactoryImpl() {}

FactoryImpl::~FactoryImpl() {}

std::unique_ptr<CryptohomeFrontend> FactoryImpl::GetCryptohomeFrontend() {
  return std::make_unique<CryptohomeFrontendImpl>(
      Middleware(middleware_.Derive()));
}

}  // namespace hwsec
