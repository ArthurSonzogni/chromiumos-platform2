// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_BACKEND_H_
#define LIBHWSEC_BACKEND_TPM2_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trunks/command_transceiver.h>
#include <trunks/trunks_factory.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy.h"

namespace hwsec {

class BackendTpm2 : public Backend {
 public:
  class StateTpm2 : public State, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsEnabled() override;
    StatusOr<bool> IsReady() override;
    Status Prepare() override;
  };

  class RandomTpm2 : public Random, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> RandomBlob(size_t size) override;
    StatusOr<brillo::SecureBlob> RandomSecureBlob(size_t size) override;
  };

  BackendTpm2(Proxy& proxy, MiddlewareDerivative middleware_derivative);

  ~BackendTpm2() override;

  void set_middleware_derivative_for_test(
      MiddlewareDerivative middleware_derivative) {
    middleware_derivative_ = middleware_derivative;
  }

 private:
  // This structure holds all Trunks client objects.
  struct TrunksClientContext {
    trunks::CommandTransceiver& command_transceiver;
    trunks::TrunksFactory& factory;
    std::unique_ptr<trunks::TpmState> tpm_state;
    std::unique_ptr<trunks::TpmUtility> tpm_utility;
  };

  State* GetState() override { return &state_; }
  DAMitigation* GetDAMitigation() override { return nullptr; }
  Storage* GetStorage() override { return nullptr; }
  RoData* GetRoData() override { return nullptr; }
  Sealing* GetSealing() override { return nullptr; }
  SignatureSealing* GetSignatureSealing() override { return nullptr; }
  Deriving* GetDeriving() override { return nullptr; }
  Encryption* GetEncryption() override { return nullptr; }
  Signing* GetSigning() override { return nullptr; }
  KeyManagerment* GetKeyManagerment() override { return nullptr; }
  SessionManagerment* GetSessionManagerment() override { return nullptr; }
  Config* GetConfig() override { return nullptr; }
  Random* GetRandom() override { return &random_; }
  PinWeaver* GetPinWeaver() override { return nullptr; }
  Vendor* GetVendor() override { return nullptr; }

  Proxy& proxy_;

  TrunksClientContext trunks_context_;

  StateTpm2 state_{*this};
  RandomTpm2 random_{*this};

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_BACKEND_H_
