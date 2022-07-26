// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_CONFIG_H_
#define LIBHWSEC_BACKEND_TPM1_CONFIG_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

extern const int kCurrentUserPcrTpm1;

class BackendTpm1;

class ConfigTpm1 : public Backend::Config,
                   public Backend::SubClassHelper<BackendTpm1> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<OperationPolicy> ToOperationPolicy(
      const OperationPolicySetting& policy) override;
  Status SetCurrentUser(const std::string& current_user) override;
  StatusOr<bool> IsCurrentUserSet() override;
  StatusOr<QuoteResult> Quote(DeviceConfigs device_config, Key key) override;

  using PcrMap = std::map<uint32_t, brillo::Blob>;

  // Converts a device config usage into a PCR map.
  StatusOr<PcrMap> ToPcrMap(const DeviceConfigs& device_config);

  // Converts a device config usage into a PCR map, and fill the value with
  // real PCR value.
  StatusOr<PcrMap> ToCurrentPcrValueMap(const DeviceConfigs& device_config);

  // Converts a device config setting into a PCR map.
  StatusOr<PcrMap> ToSettingsPcrMap(const DeviceConfigSettings& settings);

 private:
  StatusOr<brillo::Blob> ReadPcr(uint32_t pcr_index);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_CONFIG_H_
