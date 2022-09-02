// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_BACKEND_H_
#define LIBHWSEC_BACKEND_TPM1_BACKEND_H_

#include <memory>
#include <optional>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/tpm1/config.h"
#include "libhwsec/backend/tpm1/da_mitigation.h"
#include "libhwsec/backend/tpm1/deriving.h"
#include "libhwsec/backend/tpm1/encryption.h"
#include "libhwsec/backend/tpm1/key_management.h"
#include "libhwsec/backend/tpm1/pinweaver.h"
#include "libhwsec/backend/tpm1/random.h"
#include "libhwsec/backend/tpm1/recovery_crypto.h"
#include "libhwsec/backend/tpm1/sealing.h"
#include "libhwsec/backend/tpm1/signature_sealing.h"
#include "libhwsec/backend/tpm1/signing.h"
#include "libhwsec/backend/tpm1/state.h"
#include "libhwsec/backend/tpm1/storage.h"
#include "libhwsec/backend/tpm1/vendor.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

namespace hwsec {

class BackendTpm1 : public Backend {
 public:
  // This structure holds all Overalls client objects.
  struct OverallsContext {
    overalls::Overalls& overalls;
  };

  BackendTpm1(Proxy& proxy, MiddlewareDerivative middleware_derivative);

  ~BackendTpm1() override;

  MiddlewareDerivative GetMiddlewareDerivative() const {
    return middleware_derivative_;
  }

  Proxy& GetProxy() { return proxy_; }
  OverallsContext& GetOverall() { return overall_context_; }

  StatusOr<ScopedTssContext> GetScopedTssContext();
  StatusOr<TSS_HCONTEXT> GetTssContext();
  StatusOr<TSS_HTPM> GetUserTpmHandle();

  // The delegate TPM handle would not be cached to prevent leaking the delegate
  // permission.
  StatusOr<ScopedTssObject<TSS_HTPM>> GetDelegateTpmHandle();

  StateTpm1& GetStateTpm1() { return state_; }
  DAMitigationTpm1& GetDAMitigationTpm1() { return da_mitigation_; }
  StorageTpm1& GetStorageTpm1() { return storage_; }
  SealingTpm1& GetSealingTpm1() { return sealing_; }
  SignatureSealingTpm1& GetSignatureSealingTpm1() { return signature_sealing_; }
  DerivingTpm1& GetDerivingTpm1() { return deriving_; }
  EncryptionTpm1& GetEncryptionTpm1() { return encryption_; }
  SigningTpm1& GetSigningTpm1() { return signing_; }
  KeyManagementTpm1& GetKeyManagementTpm1() { return key_management_; }
  ConfigTpm1& GetConfigTpm1() { return config_; }
  RandomTpm1& GetRandomTpm1() { return random_; }
  PinWeaverTpm1& GetPinWeaverTpm1() { return pinweaver_; }
  VendorTpm1& GetVendorTpm1() { return vendor_; }
  RecoveryCryptoTpm1& GetRecoveryCryptoTpm1() { return recovery_crypto_; }

  void set_middleware_derivative_for_test(
      MiddlewareDerivative middleware_derivative) {
    middleware_derivative_ = middleware_derivative;
  }

 private:
  State* GetState() override { return &state_; }
  DAMitigation* GetDAMitigation() override { return &da_mitigation_; }
  Storage* GetStorage() override { return &storage_; }
  RoData* GetRoData() override { return nullptr; }
  Sealing* GetSealing() override { return &sealing_; }
  SignatureSealing* GetSignatureSealing() override {
    return &signature_sealing_;
  }
  Deriving* GetDeriving() override { return &deriving_; }
  Encryption* GetEncryption() override { return &encryption_; }
  Signing* GetSigning() override { return &signing_; }
  KeyManagement* GetKeyManagement() override { return &key_management_; }
  SessionManagement* GetSessionManagement() override { return nullptr; }
  Config* GetConfig() override { return &config_; }
  Random* GetRandom() override { return &random_; }
  PinWeaver* GetPinWeaver() override { return &pinweaver_; }
  Vendor* GetVendor() override { return &vendor_; }
  RecoveryCrypto* GetRecoveryCrypto() override { return &recovery_crypto_; }

  Proxy& proxy_;
  OverallsContext overall_context_;
  std::optional<ScopedTssContext> tss_context_;
  std::optional<ScopedTssObject<TSS_HTPM>> user_tpm_handle_;

  StateTpm1 state_{*this};
  DAMitigationTpm1 da_mitigation_{*this};
  StorageTpm1 storage_{*this};
  SealingTpm1 sealing_{*this};
  SignatureSealingTpm1 signature_sealing_{*this};
  DerivingTpm1 deriving_{*this};
  EncryptionTpm1 encryption_{*this};
  SigningTpm1 signing_{*this};
  KeyManagementTpm1 key_management_{*this};
  ConfigTpm1 config_{*this};
  RandomTpm1 random_{*this};
  PinWeaverTpm1 pinweaver_{*this};
  VendorTpm1 vendor_{*this};
  RecoveryCryptoTpm1 recovery_crypto_{*this};

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_BACKEND_H_
