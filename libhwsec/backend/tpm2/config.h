// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_CONFIG_H_
#define LIBHWSEC_BACKEND_TPM2_CONFIG_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>
#include <trunks/command_transceiver.h>
#include <trunks/trunks_factory.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class BackendTpm2;

class ConfigTpm2 : public Backend::Config,
                   public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<OperationPolicy> ToOperationPolicy(
      const OperationPolicySetting& policy) override;
  Status SetCurrentUser(const std::string& current_user) override;
  StatusOr<bool> IsCurrentUserSet() override;
  StatusOr<QuoteResult> Quote(DeviceConfigs device_config, Key key) override;

  using PcrMap = std::map<uint32_t, std::string>;
  struct TrunksSession {
    using InnerSession = std::variant<std::unique_ptr<trunks::HmacSession>,
                                      std::unique_ptr<trunks::PolicySession>>;
    InnerSession session;
    trunks::AuthorizationDelegate* delegate;
  };

  // Defines a set of PCR indexes (in bitmask) and the digest that is valid
  // after computation of sha256 of concatenation of PCR values included in
  // bitmask.
  struct PcrValue {
    // The set of PCR indexes that have to pass the validation.
    uint8_t bitmask[2];
    // The hash digest of the PCR values contained in the bitmask.
    std::string digest;
  };

  // Converts a device config usage into a PCR map.
  StatusOr<PcrMap> ToPcrMap(const DeviceConfigs& device_config);

  // Converts a device config setting into a PCR map.
  StatusOr<PcrMap> ToSettingsPcrMap(const DeviceConfigSettings& settings);

  // Creates a trunks policy session from |policy|, and PolicyOR the
  // |extra_policy_digests| if it's not empty.
  StatusOr<std::unique_ptr<trunks::PolicySession>> GetTrunksPolicySession(
      const OperationPolicy& policy,
      const std::vector<std::string>& extra_policy_digests,
      bool salted,
      bool enable_encryption);

  // Creates a unified session from |policy|.
  StatusOr<TrunksSession> GetTrunksSession(const OperationPolicy& policy,
                                           bool salted,
                                           bool enable_encryption);

  // Creates the PCR value for PinWeaver digest.
  StatusOr<PcrValue> ToPcrValue(const DeviceConfigSettings& settings);

  // Creates the policy digest from device config setting.
  StatusOr<std::string> ToPolicyDigest(const DeviceConfigSettings& settings);

 private:
  StatusOr<std::string> ReadPcr(uint32_t pcr_index);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_CONFIG_H_
