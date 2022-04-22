// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_BACKEND_H_
#define LIBHWSEC_BACKEND_TPM1_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <base/gtest_prod_util.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy.h"

namespace hwsec {

class BackendTpm1 : public Backend {
 public:
  class StateTpm1 : public State, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsEnabled() override;
    StatusOr<bool> IsReady() override;
    Status Prepare() override;
  };

  class ConfigTpm1 : public Config, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<OperationPolicy> ToOperationPolicy(
        const OperationPolicySetting& policy) override;
    Status SetCurrentUser(const std::string& current_user) override;
    StatusOr<QuoteResult> Quote(DeviceConfigs device_config, Key key) override;

    using PcrMap = std::map<uint32_t, brillo::Blob>;
    StatusOr<PcrMap> ToPcrMap(const DeviceConfigs& device_config);
    StatusOr<PcrMap> ToSettingsPcrMap(const DeviceConfigSettings& settings);

   private:
    StatusOr<brillo::Blob> ReadPcr(uint32_t pcr_index);
  };

  class RandomTpm1 : public Random, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> RandomBlob(size_t size) override;
    StatusOr<brillo::SecureBlob> RandomSecureBlob(size_t size) override;
  };

  BackendTpm1(Proxy& proxy, MiddlewareDerivative middleware_derivative);

  ~BackendTpm1() override;

  void set_middleware_derivative_for_test(
      MiddlewareDerivative middleware_derivative) {
    middleware_derivative_ = middleware_derivative;
  }

 private:
  // This structure holds all Overalls client objects.
  struct OverallsContext {
    overalls::Overalls& overalls;
  };
  struct TssTpmContext {
    TSS_HCONTEXT context;
    TSS_HTPM tpm_handle;
  };

  trousers::ScopedTssContext tss_user_context_;
  std::optional<TssTpmContext> tss_user_context_cache_;

  StatusOr<trousers::ScopedTssContext> GetScopedTssContext();
  StatusOr<TssTpmContext> GetTssUserContext();

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
  Config* GetConfig() override { return &config_; }
  Random* GetRandom() override { return &random_; }
  PinWeaver* GetPinWeaver() override { return nullptr; }
  Vendor* GetVendor() override { return nullptr; }

  Proxy& proxy_;

  OverallsContext overall_context_;

  StateTpm1 state_{*this};
  ConfigTpm1 config_{*this};
  RandomTpm1 random_{*this};

  MiddlewareDerivative middleware_derivative_;

  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetScopedTssContext);
  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetTssUserContext);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_BACKEND_H_
