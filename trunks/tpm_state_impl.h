// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TPM_STATE_IMPL_H_
#define TRUNKS_TPM_STATE_IMPL_H_

#include "trunks/tpm_state.h"

#include <map>
#include <string>
#include <vector>

#include <base/functional/callback.h>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

class TrunksFactory;

// TpmStateImpl is the default implementation of the TpmState interface.
class TRUNKS_EXPORT TpmStateImpl : public TpmState {
 public:
  explicit TpmStateImpl(const TrunksFactory& factory);
  TpmStateImpl(const TpmStateImpl&) = delete;
  TpmStateImpl& operator=(const TpmStateImpl&) = delete;

  ~TpmStateImpl() override = default;

  // TpmState methods.
  TPM_RC Initialize() override;
  TPM_RC Refresh() override;
  bool IsOwnerPasswordSet() override;
  bool IsEndorsementPasswordSet() override;
  bool IsLockoutPasswordSet() override;
  bool IsOwned() override;
  bool IsInLockout() override;
  bool IsPlatformHierarchyEnabled() override;
  bool IsStorageHierarchyEnabled() override;
  bool IsEndorsementHierarchyEnabled() override;
  bool IsEnabled() override;
  bool WasShutdownOrderly() override;
  bool IsRSASupported() override;
  bool IsECCSupported() override;
  uint32_t GetLockoutCounter() override;
  uint32_t GetLockoutThreshold() override;
  uint32_t GetLockoutInterval() override;
  uint32_t GetLockoutRecovery() override;
  uint32_t GetMaxNVSize() override;
  uint32_t GetTpmFamily() override;
  uint32_t GetSpecificationLevel() override;
  uint32_t GetSpecificationRevision() override;
  uint32_t GetManufacturer() override;
  uint32_t GetTpmModel() override;
  uint64_t GetFirmwareVersion() override;
  std::string GetVendorIDString() override;
  bool GetTpmProperty(TPM_PT property, uint32_t* value) override;
  bool GetAlgorithmProperties(TPM_ALG_ID algorithm,
                              TPMA_ALGORITHM* properties) override;

 private:
  // This helper method calls TPM2_GetCapability in a loop until all available
  // capabilities of the given type are sent to the |callback|. The callback
  // returns the next property value to query if there is more data available or
  // 0 if the capability data was empty.
  using CapabilityCallback =
      base::RepeatingCallback<uint32_t(const TPMU_CAPABILITIES&)>;
  TPM_RC GetCapability(const CapabilityCallback& callback,
                       TPM_CAP capability,
                       uint32_t property,
                       uint32_t max_properties_per_call);
  // Queries TPM properties and populates tpm_properties_.
  TPM_RC CacheTpmProperties();
  uint32_t TpmPropertiesCallback(const TPMU_CAPABILITIES& capability_data);
  // Queries algorithm properties and populates algorithm_properties_.
  TPM_RC CacheAlgorithmProperties();
  uint32_t AlgorithmCallback(const TPMU_CAPABILITIES& capability_data);

  const TrunksFactory& factory_;
  bool initialized_{false};
  std::map<TPM_PT, uint32_t> tpm_properties_;
  std::map<TPM_ALG_ID, TPMA_ALGORITHM> algorithm_properties_;
};

}  // namespace trunks

#endif  // TRUNKS_TPM_STATE_IMPL_H_
