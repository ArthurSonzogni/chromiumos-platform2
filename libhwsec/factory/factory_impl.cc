// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/factory/factory_impl.h"

#include <memory>
#include <utility>

#include "libhwsec/backend/backend.h"
#include "libhwsec/frontend/arc_attestation/frontend_impl.h"
#include "libhwsec/frontend/attestation/frontend_impl.h"
#include "libhwsec/frontend/bootlockbox/frontend_impl.h"
#include "libhwsec/frontend/chaps/frontend_impl.h"
#include "libhwsec/frontend/client/frontend_impl.h"
#include "libhwsec/frontend/cryptohome/frontend_impl.h"
#include "libhwsec/frontend/local_data_migration/frontend_impl.h"
#include "libhwsec/frontend/oobe_config/frontend_impl.h"
#include "libhwsec/frontend/optee-plugin/frontend_impl.h"
#include "libhwsec/frontend/pinweaver_manager/frontend_impl.h"
#include "libhwsec/frontend/recovery_crypto/frontend_impl.h"
#include "libhwsec/frontend/u2fd/frontend_impl.h"
#include "libhwsec/frontend/u2fd/vendor_frontend_impl.h"
#include "libhwsec/middleware/middleware.h"

namespace hwsec {

FactoryImpl::FactoryImpl(ThreadingMode mode)
    : default_middleware_(std::make_unique<MiddlewareOwner>(mode)),
      middleware_(*default_middleware_) {}

FactoryImpl::FactoryImpl(std::unique_ptr<MiddlewareOwner> middleware)
    : default_middleware_(std::move(middleware)),
      middleware_(*default_middleware_) {}

FactoryImpl::~FactoryImpl() {}

std::unique_ptr<const CryptohomeFrontend> FactoryImpl::GetCryptohomeFrontend() {
  return std::make_unique<CryptohomeFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const PinWeaverManagerFrontend>
FactoryImpl::GetPinWeaverManagerFrontend() {
  return std::make_unique<PinWeaverManagerFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const RecoveryCryptoFrontend>
FactoryImpl::GetRecoveryCryptoFrontend() {
  return std::make_unique<RecoveryCryptoFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const ClientFrontend> FactoryImpl::GetClientFrontend() {
  return std::make_unique<ClientFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const ChapsFrontend> FactoryImpl::GetChapsFrontend() {
  return std::make_unique<ChapsFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const U2fFrontend> FactoryImpl::GetU2fFrontend() {
  return std::make_unique<U2fFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const U2fVendorFrontend> FactoryImpl::GetU2fVendorFrontend() {
  return std::make_unique<U2fVendorFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const OpteePluginFrontend>
FactoryImpl::GetOpteePluginFrontend() {
  return std::make_unique<OpteePluginFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const BootLockboxFrontend>
FactoryImpl::GetBootLockboxFrontend() {
  return std::make_unique<BootLockboxFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const OobeConfigFrontend> FactoryImpl::GetOobeConfigFrontend() {
  return std::make_unique<OobeConfigFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const LocalDataMigrationFrontend>
FactoryImpl::GetLocalDataMigrationFrontend() {
  return std::make_unique<LocalDataMigrationFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const AttestationFrontend>
FactoryImpl::GetAttestationFrontend() {
  return std::make_unique<AttestationFrontendImpl>(middleware_.Derive());
}

std::unique_ptr<const ArcAttestationFrontend>
FactoryImpl::GetArcAttestationFrontend() {
  return std::make_unique<ArcAttestationFrontendImpl>(middleware_.Derive());
}

}  // namespace hwsec
