// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_PINWEAVER_H_
#define LIBHWSEC_BACKEND_TPM2_PINWEAVER_H_

#include <cstdint>
#include <optional>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class PinWeaverTpm2 : public Backend::PinWeaver,
                      public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<bool> IsEnabled() override;
  StatusOr<uint8_t> GetVersion() override;
  StatusOr<CredentialTreeResult> Reset(uint32_t bits_per_level,
                                       uint32_t length_labels) override;
  StatusOr<CredentialTreeResult> InsertCredential(
      const std::vector<OperationPolicySetting>& policies,
      const uint64_t label,
      const std::vector<brillo::Blob>& h_aux,
      const brillo::SecureBlob& le_secret,
      const brillo::SecureBlob& he_secret,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_schedule) override;
  StatusOr<CredentialTreeResult> CheckCredential(
      const uint64_t label,
      const std::vector<brillo::Blob>& h_aux,
      const brillo::Blob& orig_cred_metadata,
      const brillo::SecureBlob& le_secret) override;
  StatusOr<CredentialTreeResult> RemoveCredential(
      const uint64_t label,
      const std::vector<std::vector<uint8_t>>& h_aux,
      const std::vector<uint8_t>& mac) override;
  StatusOr<CredentialTreeResult> ResetCredential(
      const uint64_t label,
      const std::vector<std::vector<uint8_t>>& h_aux,
      const std::vector<uint8_t>& orig_cred_metadata,
      const brillo::SecureBlob& reset_secret) override;
  StatusOr<GetLogResult> GetLog(
      const std::vector<uint8_t>& cur_disk_root_hash) override;
  StatusOr<ReplayLogOperationResult> ReplayLogOperation(
      const brillo::Blob& log_entry_root,
      const std::vector<brillo::Blob>& h_aux,
      const brillo::Blob& orig_cred_metadata) override;
  StatusOr<int> GetWrongAuthAttempts(
      const brillo::Blob& cred_metadata) override;
  StatusOr<DelaySchedule> GetDelaySchedule(
      const brillo::Blob& cred_metadata) override;
  StatusOr<uint32_t> GetDelayInSeconds(
      const brillo::Blob& cred_metadata) override;

 private:
  // The protocol version used by pinweaver.
  std::optional<uint8_t> protocol_version_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_PINWEAVER_H_
