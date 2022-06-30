// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_PINWEAVER_FRONTEND_H_
#define LIBHWSEC_FRONTEND_PINWEAVER_FRONTEND_H_

#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class HWSEC_EXPORT PinWeaverFrontend : public Frontend {
 public:
  using CredentialTreeResult = Backend::PinWeaver::CredentialTreeResult;
  using GetLogResult = Backend::PinWeaver::GetLogResult;
  using ReplayLogOperationResult = Backend::PinWeaver::ReplayLogOperationResult;
  using DelaySchedule = Backend::PinWeaver::DelaySchedule;

  ~PinWeaverFrontend() override = default;

  // Is the pinweaver enabled or not.
  virtual StatusOr<bool> IsEnabled() = 0;

  // Gets the version of pinweaver.
  virtual StatusOr<uint8_t> GetVersion() = 0;

  // Resets the PinWeaver Hash Tree root hash with its initial known value,
  // which assumes all MACs are all-zero.
  //
  // This function should be executed only when we are setting up a hash tree
  // on a new / wiped device, or resetting the hash tree due to an
  // unrecoverable error.
  //
  // |bits_per_level| is the number of bits per level of the hash tree.
  // |length_labels| is the length of the leaf bit string.
  //
  // In all cases, the resulting root hash is returned in |new_root|.
  virtual StatusOr<CredentialTreeResult> Reset(uint32_t bits_per_level,
                                               uint32_t length_labels) = 0;

  // Tries to insert a credential into the TPM.
  //
  // The label of the leaf node is in |label|, the list of auxiliary hashes is
  // in |h_aux|, the LE credential to be added is in |le_secret|. Along with
  // it, its associated reset_secret |reset_secret| and the high entropy
  // credential it protects |he_secret| are also provided. The delay schedule
  // which determines the delay enforced between authentication attempts is
  // provided by |delay_schedule|. And the credential is bound to the the
  // |policies|, the check credential operation would only success when one of
  // policy match.
  //
  // |h_aux| requires a particular order: starting from left child to right
  // child, from leaf upwards till the children of the root label.
  //
  // If successful, the new credential metadata will be placed in the blob
  // pointed by |new_cred_metadata|. The MAC of the credential will be
  // returned in |new_mac|. The resulting root hash is returned in |new_root|.
  //
  // In all cases, the resulting root hash is returned in |new_root|.
  virtual StatusOr<CredentialTreeResult> InsertCredential(
      const std::vector<OperationPolicySetting>& policies,
      const uint64_t label,
      const std::vector<brillo::Blob>& h_aux,
      const brillo::SecureBlob& le_secret,
      const brillo::SecureBlob& he_secret,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_schedule) = 0;

  // Tries to verify/authenticate a credential.
  //
  // The obfuscated LE Credential is |le_secret| and the credential metadata
  // is in |orig_cred_metadata|.
  //
  // On success, or failure due to an invalid |le_secret|, the updated
  // credential metadata and corresponding new MAC will be returned in
  // |new_cred_metadata| and |new_mac|.
  //
  // On success, the released high entropy credential will be returned in
  // |he_secret| and the reset secret in |reset_secret|.
  //
  // In all cases, the resulting root hash is returned in |new_root|.
  virtual StatusOr<CredentialTreeResult> CheckCredential(
      const uint64_t label,
      const std::vector<brillo::Blob>& h_aux,
      const brillo::Blob& orig_cred_metadata,
      const brillo::SecureBlob& le_secret) = 0;

  // Removes the credential which has label |label|.
  //
  // The corresponding list of auxiliary hashes is in |h_aux|, and the MAC of
  // the label that needs to be removed is |mac|.
  //
  // In all cases, the resulting root hash is returned in |new_root|.
  virtual StatusOr<CredentialTreeResult> RemoveCredential(
      const uint64_t label,
      const std::vector<std::vector<uint8_t>>& h_aux,
      const std::vector<uint8_t>& mac) = 0;

  // Tries to reset a (potentially locked out) credential.
  //
  // The reset credential is |reset_secret| and the credential metadata is
  // in |orig_cred_metadata|.
  //
  // On success, the updated credential metadata and corresponding new MAC
  // will be returned in |new_cred_metadata| and |new_mac|.
  //
  // In all cases, the resulting root hash is returned in |new_root|.
  virtual StatusOr<CredentialTreeResult> ResetCredential(
      const uint64_t label,
      const std::vector<std::vector<uint8_t>>& h_aux,
      const std::vector<uint8_t>& orig_cred_metadata,
      const brillo::SecureBlob& reset_secret) = 0;

  // Retrieves the replay log.
  //
  // The current on-disk root hash is supplied via |cur_disk_root_hash|.
  virtual StatusOr<GetLogResult> GetLog(
      const std::vector<uint8_t>& cur_disk_root_hash) = 0;

  // Replays the log operation referenced by |log_entry_root|, where
  // |log_entry_root| is the resulting root hash after the operation, and is
  // retrieved from the log entry.
  // |h_aux| and |orig_cred_metadata| refer to, respectively, the list of
  // auxiliary hashes and the original credential metadata associated with the
  // label concerned (available in the log entry). The resulting metadata and
  // MAC are stored in |new_cred_metadata| and |new_mac|.
  virtual StatusOr<ReplayLogOperationResult> ReplayLogOperation(
      const brillo::Blob& log_entry_root,
      const std::vector<brillo::Blob>& h_aux,
      const brillo::Blob& orig_cred_metadata) = 0;

  // Looks into the metadata and retrieves the number of wrong authentication
  // attempts.
  virtual StatusOr<int> GetWrongAuthAttempts(
      const brillo::Blob& cred_metadata) = 0;

  // Looks into the metadata and retrieves the delay schedule.
  virtual StatusOr<DelaySchedule> GetDelaySchedule(
      const brillo::Blob& cred_metadata) = 0;

  // Get the remaining delay in seconds.
  virtual StatusOr<uint32_t> GetDelayInSeconds(
      const brillo::Blob& cred_metadata) = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_PINWEAVER_FRONTEND_H_
