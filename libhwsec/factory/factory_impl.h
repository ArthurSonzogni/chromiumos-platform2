// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FACTORY_FACTORY_IMPL_H_
#define LIBHWSEC_FACTORY_FACTORY_IMPL_H_

#include <memory>
#include <utility>

#include "libhwsec/factory/factory.h"
#include "libhwsec/frontend/arc_attestation/frontend.h"
#include "libhwsec/frontend/attestation/frontend.h"
#include "libhwsec/frontend/bootlockbox/frontend.h"
#include "libhwsec/frontend/chaps/frontend.h"
#include "libhwsec/frontend/client/frontend.h"
#include "libhwsec/frontend/cryptohome/frontend.h"
#include "libhwsec/frontend/local_data_migration/frontend.h"
#include "libhwsec/frontend/oobe_config/frontend.h"
#include "libhwsec/frontend/optee-plugin/frontend.h"
#include "libhwsec/frontend/pinweaver/frontend.h"
#include "libhwsec/frontend/recovery_crypto/frontend.h"
#include "libhwsec/frontend/u2fd/frontend.h"
#include "libhwsec/frontend/u2fd/vendor_frontend.h"
#include "libhwsec/structures/threading_mode.h"

namespace hwsec {

// Forward declarations
class MiddlewareOwner;

class HWSEC_EXPORT FactoryImpl : public Factory {
 public:
  explicit FactoryImpl(
      ThreadingMode mode = ThreadingMode::kStandaloneWorkerThread);

  // Constructor for custom middleware.
  explicit FactoryImpl(std::unique_ptr<MiddlewareOwner> middleware);

  ~FactoryImpl() override;

  std::unique_ptr<const CryptohomeFrontend> GetCryptohomeFrontend() override;
  std::unique_ptr<const PinWeaverFrontend> GetPinWeaverFrontend() override;
  std::unique_ptr<const PinWeaverManagerFrontend> GetPinWeaverManagerFrontend()
      override;
  std::unique_ptr<const RecoveryCryptoFrontend> GetRecoveryCryptoFrontend()
      override;
  std::unique_ptr<const ClientFrontend> GetClientFrontend() override;
  std::unique_ptr<const ChapsFrontend> GetChapsFrontend() override;
  std::unique_ptr<const U2fFrontend> GetU2fFrontend() override;
  std::unique_ptr<const U2fVendorFrontend> GetU2fVendorFrontend() override;
  std::unique_ptr<const OpteePluginFrontend> GetOpteePluginFrontend() override;
  std::unique_ptr<const BootLockboxFrontend> GetBootLockboxFrontend() override;
  std::unique_ptr<const OobeConfigFrontend> GetOobeConfigFrontend() override;
  std::unique_ptr<const LocalDataMigrationFrontend>
  GetLocalDataMigrationFrontend() override;
  std::unique_ptr<const AttestationFrontend> GetAttestationFrontend() override;
  std::unique_ptr<const ArcAttestationFrontend> GetArcAttestationFrontend()
      override;

 protected:
  std::unique_ptr<MiddlewareOwner> default_middleware_;
  MiddlewareOwner& middleware_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FACTORY_FACTORY_IMPL_H_
