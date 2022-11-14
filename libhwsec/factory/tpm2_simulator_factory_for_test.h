// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FACTORY_TPM2_SIMULATOR_FACTORY_FOR_TEST_H_
#define LIBHWSEC_FACTORY_TPM2_SIMULATOR_FACTORY_FOR_TEST_H_

#include <memory>
#include <utility>

#include "libhwsec/factory/factory.h"
#include "libhwsec/frontend/chaps/frontend.h"
#include "libhwsec/frontend/client/frontend.h"
#include "libhwsec/frontend/cryptohome/frontend.h"
#include "libhwsec/frontend/pinweaver/frontend.h"
#include "libhwsec/frontend/recovery_crypto/frontend.h"
#include "libhwsec/frontend/u2fd/frontend.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/structures/threading_mode.h"

namespace hwsec {

// Forward declarations
class Backend;
class MiddlewareOwner;
class Proxy;

// A TPM2 simulator factory implementation for testing.
//
// The default mode will run the middleware on current task runner, but that
// need to be used carefully in multi-thread environment.
//
// Example usage:
//   Tpm2SimulatorFactoryForTest factory;
//   StatusOr<bool> ready = factory.GetCryptohomeFrontend()->IsReady();

class HWSEC_EXPORT Tpm2SimulatorFactoryForTest : public Factory {
 public:
  explicit Tpm2SimulatorFactoryForTest(
      ThreadingMode mode = ThreadingMode::kCurrentThread);
  ~Tpm2SimulatorFactoryForTest() override;
  std::unique_ptr<CryptohomeFrontend> GetCryptohomeFrontend() override;
  std::unique_ptr<PinWeaverFrontend> GetPinWeaverFrontend() override;
  std::unique_ptr<RecoveryCryptoFrontend> GetRecoveryCryptoFrontend() override;
  std::unique_ptr<ClientFrontend> GetClientFrontend() override;
  std::unique_ptr<ChapsFrontend> GetChapsFrontend() override;
  std::unique_ptr<U2fFrontend> GetU2fFrontend() override;

 private:
  std::unique_ptr<Proxy> proxy_;
  std::unique_ptr<Backend> backend_;
  std::unique_ptr<MiddlewareOwner> middleware_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FACTORY_TPM2_SIMULATOR_FACTORY_FOR_TEST_H_
