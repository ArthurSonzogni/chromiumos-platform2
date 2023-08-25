// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FACTORY_FACTORY_H_
#define LIBHWSEC_FACTORY_FACTORY_H_

#include <memory>
#include <utility>

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
#include "libhwsec/frontend/pinweaver_manager/frontend.h"
#include "libhwsec/frontend/recovery_crypto/frontend.h"
#include "libhwsec/frontend/u2fd/frontend.h"
#include "libhwsec/frontend/u2fd/vendor_frontend.h"
#include "libhwsec/hwsec_export.h"

// Factory holds the ownership of the middleware and backend.
// And generates different frontend for different usage.

namespace hwsec {

class Factory {
 public:
  virtual ~Factory() = default;
  virtual std::unique_ptr<const CryptohomeFrontend> GetCryptohomeFrontend() = 0;
  virtual std::unique_ptr<const PinWeaverFrontend> GetPinWeaverFrontend() = 0;
  virtual std::unique_ptr<const PinWeaverManagerFrontend>
  GetPinWeaverManagerFrontend() = 0;
  virtual std::unique_ptr<const RecoveryCryptoFrontend>
  GetRecoveryCryptoFrontend() = 0;
  virtual std::unique_ptr<const ClientFrontend> GetClientFrontend() = 0;
  virtual std::unique_ptr<const ChapsFrontend> GetChapsFrontend() = 0;
  virtual std::unique_ptr<const U2fFrontend> GetU2fFrontend() = 0;
  virtual std::unique_ptr<const U2fVendorFrontend> GetU2fVendorFrontend() = 0;
  virtual std::unique_ptr<const OpteePluginFrontend>
  GetOpteePluginFrontend() = 0;
  virtual std::unique_ptr<const BootLockboxFrontend>
  GetBootLockboxFrontend() = 0;
  virtual std::unique_ptr<const OobeConfigFrontend> GetOobeConfigFrontend() = 0;
  virtual std::unique_ptr<const LocalDataMigrationFrontend>
  GetLocalDataMigrationFrontend() = 0;
  virtual std::unique_ptr<const AttestationFrontend>
  GetAttestationFrontend() = 0;
  virtual std::unique_ptr<const ArcAttestationFrontend>
  GetArcAttestationFrontend() = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FACTORY_FACTORY_H_
