// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_BACKEND_H_
#define LIBHWSEC_BACKEND_TPM2_BACKEND_H_

#include <memory>
#include <trunks/command_transceiver.h>
#include <trunks/trunks_factory.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/tpm2/config.h"
#include "libhwsec/backend/tpm2/da_mitigation.h"
#include "libhwsec/backend/tpm2/deriving.h"
#include "libhwsec/backend/tpm2/encryption.h"
#include "libhwsec/backend/tpm2/key_management.h"
#include "libhwsec/backend/tpm2/pinweaver.h"
#include "libhwsec/backend/tpm2/random.h"
#include "libhwsec/backend/tpm2/sealing.h"
#include "libhwsec/backend/tpm2/signature_sealing.h"
#include "libhwsec/backend/tpm2/state.h"
#include "libhwsec/backend/tpm2/storage.h"
#include "libhwsec/backend/tpm2/vendor.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy.h"

namespace hwsec {

class BackendTpm2 : public Backend {
 public:
  BackendTpm2(Proxy& proxy, MiddlewareDerivative middleware_derivative);

  ~BackendTpm2() override;

  // This structure holds all Trunks client objects.
  struct TrunksClientContext {
    trunks::CommandTransceiver& command_transceiver;
    trunks::TrunksFactory& factory;
    std::unique_ptr<trunks::TpmState> tpm_state;
    std::unique_ptr<trunks::TpmUtility> tpm_utility;
  };

  MiddlewareDerivative GetMiddlewareDerivative() const {
    return middleware_derivative_;
  }

  Proxy& GetProxy() { return proxy_; }
  TrunksClientContext& GetTrunksContext() { return trunks_context_; }

  StateTpm2& GetStateTpm2() { return state_; }
  DAMitigationTpm2& GetDAMitigationTpm2() { return da_mitigation_; }
  StorageTpm2& GetStorageTpm2() { return storage_; }
  SealingTpm2& GetSealingTpm2() { return sealing_; }
  SignatureSealingTpm2& GetSignatureSealingTpm2() { return signature_sealing_; }
  DerivingTpm2& GetDerivingTpm2() { return deriving_; }
  EncryptionTpm2& GetEncryptionTpm2() { return encryption_; }
  KeyManagementTpm2& GetKeyManagementTpm2() { return key_management_; }
  ConfigTpm2& GetConfigTpm2() { return config_; }
  RandomTpm2& GetRandomTpm2() { return random_; }
  PinWeaverTpm2& GetPinWeaverTpm2() { return pinweaver_; }
  VendorTpm2& GetVendorTpm2() { return vendor_; }

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
  Signing* GetSigning() override { return nullptr; }
  KeyManagement* GetKeyManagement() override { return &key_management_; }
  SessionManagement* GetSessionManagement() override { return nullptr; }
  Config* GetConfig() override { return &config_; }
  Random* GetRandom() override { return &random_; }
  PinWeaver* GetPinWeaver() override { return &pinweaver_; }
  Vendor* GetVendor() override { return &vendor_; }

  Proxy& proxy_;

  TrunksClientContext trunks_context_;

  StateTpm2 state_{*this};
  DAMitigationTpm2 da_mitigation_{*this};
  StorageTpm2 storage_{*this};
  SealingTpm2 sealing_{*this};
  SignatureSealingTpm2 signature_sealing_{*this};
  DerivingTpm2 deriving_{*this};
  EncryptionTpm2 encryption_{*this};
  KeyManagementTpm2 key_management_{*this};
  ConfigTpm2 config_{*this};
  RandomTpm2 random_{*this};
  PinWeaverTpm2 pinweaver_{*this};
  VendorTpm2 vendor_{*this};

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_BACKEND_H_
