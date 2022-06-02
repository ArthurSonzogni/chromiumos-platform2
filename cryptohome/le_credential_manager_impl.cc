// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/le_credential_manager_impl.h"

#include <fcntl.h>

#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"

using ::cryptohome::error::CryptohomeLECredError;
using ::cryptohome::error::ErrorAction;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::GetSecureRandom;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

namespace cryptohome {

LECredentialManagerImpl::LECredentialManagerImpl(
    LECredentialBackend* le_backend, const base::FilePath& le_basedir)
    : is_locked_(false), le_tpm_backend_(le_backend), basedir_(le_basedir) {
  CHECK(le_tpm_backend_);

  // Check if hash tree already exists.
  bool new_hash_tree = !base::PathExists(le_basedir);

  hash_tree_ = std::make_unique<SignInHashTree>(kLengthLabels, kBitsPerLevel,
                                                le_basedir);
  if (!hash_tree_->IsValid()) {
    LOG(ERROR)
        << "Failed to initialize LE credential manager: invalid hash tree";
    return;
  }

  // Reset the root hash in the TPM to its initial value.
  if (new_hash_tree) {
    CHECK(le_tpm_backend_->Reset(&root_hash_));
    hash_tree_->GenerateAndStoreHashCache();
  }
}

LECredStatus LECredentialManagerImpl::InsertCredential(
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_sched,
    const ValidPcrCriteria& valid_pcr_criteria,
    uint64_t* ret_label) {
  if (!hash_tree_->IsValid() || !Sync()) {
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManInvalidTreeInInsertCred),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }

  SignInHashTree::Label label = hash_tree_->GetFreeLabel();
  if (!label.is_valid()) {
    LOG(ERROR) << "No free labels available.";
    ReportLEResult(kLEOpInsert, kLEActionLoadFromDisk,
                   LE_CRED_ERROR_NO_FREE_LABEL);
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManLabelUnavailableInInsertCred),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_NO_FREE_LABEL);
  }

  std::vector<std::vector<uint8_t>> h_aux = GetAuxHashes(label);
  if (h_aux.empty()) {
    LOG(ERROR) << "Error getting aux hashes for label: " << label.value();
    ReportLEResult(kLEOpInsert, kLEActionLoadFromDisk, LE_CRED_ERROR_HASH_TREE);
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManEmptyAuxInInsertCred),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }

  ReportLEResult(kLEOpInsert, kLEActionLoadFromDisk, LE_CRED_SUCCESS);

  std::vector<uint8_t> cred_metadata, mac;
  bool success = le_tpm_backend_->InsertCredential(
      label.value(), h_aux, le_secret, he_secret, reset_secret, delay_sched,
      valid_pcr_criteria, &cred_metadata, &mac, &root_hash_);
  if (!success) {
    LOG(ERROR) << "Error executing TPM InsertCredential command.";
    ReportLEResult(kLEOpInsert, kLEActionBackend, LE_CRED_ERROR_HASH_TREE);
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManTpmFailedInInsertCred),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }

  ReportLEResult(kLEOpInsert, kLEActionBackend, LE_CRED_SUCCESS);

  if (!hash_tree_->StoreLabel(label, mac, cred_metadata, false)) {
    ReportLEResult(kLEOpInsert, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR)
        << "InsertCredential succeeded in TPM but disk updated failed, label: "
        << label.value();
    // The insert into the disk hash tree failed, so let us remove
    // the credential from the TPM state so that we are back to where
    // we started.
    success = le_tpm_backend_->RemoveCredential(label.value(), h_aux, mac,
                                                &root_hash_);
    if (!success) {
      ReportLEResult(kLEOpInsert, kLEActionBackend, LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << " Failed to rewind aborted InsertCredential in TPM, label: "
                 << label.value();
      // The attempt to undo the TPM side operation has also failed, Can't do
      // much else now. We block further LE operations until at least the next
      // boot.
      is_locked_ = true;
      // TODO(crbug.com/809749): Report failure to UMA.
    }
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManStoreFailedInInsertCred),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }

  ReportLEResult(kLEOpInsert, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  *ret_label = label.value();
  return OkStatus<CryptohomeLECredError>();
}

LECredStatus LECredentialManagerImpl::CheckCredential(
    const uint64_t& label,
    const brillo::SecureBlob& le_secret,
    brillo::SecureBlob* he_secret,
    brillo::SecureBlob* reset_secret) {
  return CheckSecret(label, le_secret, he_secret, reset_secret, true);
}

LECredStatus LECredentialManagerImpl::ResetCredential(
    const uint64_t& label, const brillo::SecureBlob& reset_secret) {
  return CheckSecret(label, reset_secret, nullptr, nullptr, false);
}

LECredStatus LECredentialManagerImpl::RemoveCredential(const uint64_t& label) {
  if (!hash_tree_->IsValid() || !Sync()) {
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManInvalidTreeInRemoveCred),
        ErrorActionSet({ErrorAction::kReboot}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);
  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredStatus ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                       &h_aux, &metadata_lost);
  if (!ret.ok()) {
    ReportLEResult(kLEOpRemove, kLEActionLoadFromDisk,
                   ret->local_lecred_error());
    return MakeStatus<CryptohomeLECredError>(
               CRYPTOHOME_ERR_LOC(kLocLECredManRetrieveLabelFailedInRemoveCred))
        .Wrap(std::move(ret));
  }

  bool success =
      le_tpm_backend_->RemoveCredential(label, h_aux, orig_mac, &root_hash_);
  if (!success) {
    ReportLEResult(kLEOpRemove, kLEActionBackend, LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "Error executing TPM RemoveCredential command.";
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManRemoveCredFailedInRemoveCred),
        ErrorActionSet({ErrorAction::kReboot}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }
  ReportLEResult(kLEOpRemove, kLEActionBackend, LE_CRED_SUCCESS);

  if (!hash_tree_->RemoveLabel(label_object)) {
    LOG(ERROR) << "Removed label from TPM but hash tree removal "
                  "encountered error: "
               << label;
    ReportLEResult(kLEOpRemove, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    // This is an un-salvageable state. We can't make LE updates anymore,
    // since the disk state can't be updated.
    // We block further LE operations until at least the next boot.
    // The hope is that on reboot, the disk operations start working. In that
    // case, we will be able to replay this operation from the TPM log.
    is_locked_ = true;
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManRemoveLabelFailedInRemoveCred),
        ErrorActionSet({ErrorAction::kReboot}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }
  ReportLEResult(kLEOpRemove, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  return OkStatus<CryptohomeLECredError>();
}

LECredStatus LECredentialManagerImpl::CheckSecret(
    const uint64_t& label,
    const brillo::SecureBlob& secret,
    brillo::SecureBlob* he_secret,
    brillo::SecureBlob* reset_secret,
    bool is_le_secret) {
  if (!hash_tree_->IsValid() || !Sync()) {
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManInvalidTreeInCheckSecret),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }

  const char* uma_log_op = is_le_secret ? kLEOpCheck : kLEOpReset;

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredStatus ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                       &h_aux, &metadata_lost);
  if (!ret.ok()) {
    ReportLEResult(uma_log_op, kLEActionLoadFromDisk,
                   ret->local_lecred_error());
    return ret;
  }

  if (metadata_lost) {
    LOG(ERROR) << "Invalid cred metadata for label: " << label;
    ReportLEResult(uma_log_op, kLEActionLoadFromDisk,
                   LE_CRED_ERROR_INVALID_METADATA);
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManInvalidMetadataInCheckSecret),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_INVALID_METADATA);
  }

  ReportLEResult(uma_log_op, kLEActionLoadFromDisk, LE_CRED_SUCCESS);

  std::vector<uint8_t> new_cred, new_mac;
  LECredBackendError err;
  if (is_le_secret) {
    he_secret->clear();
    le_tpm_backend_->CheckCredential(label, h_aux, orig_cred, secret, &new_cred,
                                     &new_mac, he_secret, reset_secret, &err,
                                     &root_hash_);
  } else {
    le_tpm_backend_->ResetCredential(label, h_aux, orig_cred, secret, &new_cred,
                                     &new_mac, &err, &root_hash_);
  }

  ReportLEResult(uma_log_op, kLEActionBackend, BackendErrorToCredError(err));

  // Store the new credential meta data and MAC in case the backend performed a
  // state change. Note that this might also be needed for some failure cases.
  if (!new_cred.empty() && !new_mac.empty()) {
    if (!hash_tree_->StoreLabel(label_object, new_mac, new_cred, false)) {
      ReportLEResult(uma_log_op, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << "Failed to update credential in disk hash tree for label: "
                 << label;
      // This is an un-salvageable state. We can't make LE updates anymore,
      // since the disk state can't be updated.
      // We block further LE operations until at least the next boot.
      // The hope is that on reboot, the disk operations start working. In that
      // case, we will be able to replay this operation from the TPM log.
      is_locked_ = true;
      // TODO(crbug.com/809749): Report failure to UMA.
      return MakeStatus<CryptohomeLECredError>(
          CRYPTOHOME_ERR_LOC(kLocLECredManStoreLabelFailedInCheckSecret),
          ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
          LECredError::LE_CRED_ERROR_HASH_TREE);
    }
  }

  ReportLEResult(uma_log_op, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  LECredStatus converted = ConvertTpmError(err);
  if (converted.ok())
    return OkStatus<CryptohomeLECredError>();

  return MakeStatus<CryptohomeLECredError>(
             CRYPTOHOME_ERR_LOC(kLocLECredManTpmFailedInCheckSecret),
             ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}))
      .Wrap(std::move(converted));
}

bool LECredentialManagerImpl::NeedsPcrBinding(const uint64_t& label) {
  if (!hash_tree_->IsValid()) {
    return false;
  }
  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredStatus ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                       &h_aux, &metadata_lost);
  if (!ret.ok())
    return false;

  return le_tpm_backend_->NeedsPCRBinding(orig_cred);
}

int LECredentialManagerImpl::GetWrongAuthAttempts(const uint64_t& label) {
  if (!hash_tree_->IsValid()) {
    return -1;
  }
  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredStatus ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                       &h_aux, &metadata_lost);
  if (!ret.ok())
    return -1;

  return le_tpm_backend_->GetWrongAuthAttempts(orig_cred);
}

LECredStatus LECredentialManagerImpl::RetrieveLabelInfo(
    const SignInHashTree::Label& label,
    std::vector<uint8_t>* cred_metadata,
    std::vector<uint8_t>* mac,
    std::vector<std::vector<uint8_t>>* h_aux,
    bool* metadata_lost) {
  if (!hash_tree_->GetLabelData(label, mac, cred_metadata, metadata_lost)) {
    LOG(ERROR) << "Failed to get the credential in disk hash tree for label: "
               << label.value();
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManTreeGetDataFailedInRetrieveLabel),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_INVALID_LABEL);
  }

  // Any empty |cred_metadata| means the label isn't present in the hash tree.
  if (cred_metadata->empty()) {
    LOG(ERROR) << "Label doesn't exist in hash tree: " << label.value();
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManNonexistentInRetrieveLabel),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_INVALID_LABEL);
  }

  *h_aux = GetAuxHashes(label);
  if (h_aux->empty()) {
    LOG(ERROR) << "Error retrieving aux hashes from hash tree for label: "
               << label.value();
    return MakeStatus<CryptohomeLECredError>(
        CRYPTOHOME_ERR_LOC(kLocLECredManEmptyAuxInRetrieveLabel),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
        LECredError::LE_CRED_ERROR_HASH_TREE);
  }
  return OkStatus<CryptohomeLECredError>();
}

std::vector<std::vector<uint8_t>> LECredentialManagerImpl::GetAuxHashes(
    const SignInHashTree::Label& label) {
  auto aux_labels = hash_tree_->GetAuxiliaryLabels(label);
  std::vector<std::vector<uint8_t>> h_aux;
  if (aux_labels.empty()) {
    LOG(ERROR) << "Error getting h_aux for label:" << label.value();
    return h_aux;
  }

  h_aux.reserve(aux_labels.size());
  for (auto cur_aux_label : aux_labels) {
    brillo::Blob hash, cred_data;
    bool metadata_lost;
    if (!hash_tree_->GetLabelData(cur_aux_label, &hash, &cred_data,
                                  &metadata_lost)) {
      LOG(INFO) << "Error getting aux label :" << cur_aux_label.value()
                << " for label: " << label.value();
      h_aux.clear();
      break;
    }
    h_aux.push_back(std::move(hash));
  }

  return h_aux;
}

LECredStatus LECredentialManagerImpl::ConvertTpmError(LECredBackendError err) {
  LECredError conv_err = BackendErrorToCredError(err);
  if (conv_err == LE_CRED_SUCCESS)
    return OkStatus<CryptohomeLECredError>();

  ErrorActionSet action_set;
  if (conv_err == LE_CRED_ERROR_TOO_MANY_ATTEMPTS)
    action_set.insert(ErrorAction::kTpmLockout);

  return MakeStatus<CryptohomeLECredError>(
      CRYPTOHOME_ERR_LOC(kLocLECredManConvertTpmError), std::move(action_set),
      conv_err);
}

LECredError LECredentialManagerImpl::BackendErrorToCredError(
    LECredBackendError err) {
  switch (err) {
    case LE_TPM_SUCCESS:
      return LE_CRED_SUCCESS;
    case LE_TPM_ERROR_INVALID_LE_SECRET:
      return LE_CRED_ERROR_INVALID_LE_SECRET;
    case LE_TPM_ERROR_INVALID_RESET_SECRET:
      return LE_CRED_ERROR_INVALID_RESET_SECRET;
    case LE_TPM_ERROR_TOO_MANY_ATTEMPTS:
      return LE_CRED_ERROR_TOO_MANY_ATTEMPTS;
    case LE_TPM_ERROR_HASH_TREE_SYNC:
    case LE_TPM_ERROR_TPM_OP_FAILED:
      return LE_CRED_ERROR_HASH_TREE;
    case LE_TPM_ERROR_PCR_NOT_MATCH:
      return LE_CRED_ERROR_PCR_NOT_MATCH;
  }

  return LE_CRED_ERROR_HASH_TREE;
}

bool LECredentialManagerImpl::Sync() {
  if (is_locked_) {
    ReportLESyncOutcome(LE_CRED_ERROR_LE_LOCKED);
    return false;
  }

  std::vector<uint8_t> disk_root_hash;
  hash_tree_->GetRootHash(&disk_root_hash);

  // If we don't have it, get the root hash from the LE Backend.
  std::vector<LELogEntry> log;
  if (root_hash_.empty()) {
    if (!le_tpm_backend_->GetLog(disk_root_hash, &root_hash_, &log)) {
      ReportLEResult(kLEOpSync, kLEActionBackendGetLog,
                     LE_CRED_ERROR_UNCLASSIFIED);
      ReportLESyncOutcome(LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << "Couldn't get LE Log.";
      is_locked_ = true;
      return false;
    }
    ReportLEResult(kLEOpSync, kLEActionBackendGetLog, LE_CRED_SUCCESS);
  }

  if (disk_root_hash == root_hash_) {
    ReportLESyncOutcome(LE_CRED_SUCCESS);
    return true;
  }

  LOG(WARNING) << "LE HashCache is stale; reconstructing.";
  // TODO(crbug.com/809749): Add UMA logging for this event.
  hash_tree_->GenerateAndStoreHashCache();
  disk_root_hash.clear();
  hash_tree_->GetRootHash(&disk_root_hash);

  if (disk_root_hash == root_hash_) {
    ReportLESyncOutcome(LE_CRED_SUCCESS);
    return true;
  }

  LOG(WARNING) << "LE sync loss between OS and GSC, attempting log replay.";

  // Get the log again, since |disk_root_hash| may have changed.
  log.clear();
  if (!le_tpm_backend_->GetLog(disk_root_hash, &root_hash_, &log)) {
    ReportLEResult(kLEOpSync, kLEActionBackendGetLog,
                   LE_CRED_ERROR_UNCLASSIFIED);
    ReportLESyncOutcome(LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "Couldn't get LE Log.";
    is_locked_ = true;
    return false;
  }
  ReportLEResult(kLEOpSync, kLEActionBackendGetLog, LE_CRED_SUCCESS);
  if (!ReplayLogEntries(log, disk_root_hash)) {
    ReportLESyncOutcome(LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "Failed to Synchronize LE disk state after log replay.";
    // TODO(crbug.com/809749): Add UMA logging for this event.
    is_locked_ = true;
    return false;
  }
  ReportLESyncOutcome(LE_CRED_SUCCESS);
  return true;
}

bool LECredentialManagerImpl::ReplayInsert(uint64_t label,
                                           const std::vector<uint8_t>& log_root,
                                           const std::vector<uint8_t>& mac) {
  LOG(INFO) << "Replaying insert for label " << label;

  // Fill cred_metadata with some random data since LECredentialManager
  // considers empty cred_metadata as a non-existent label.
  std::vector<uint8_t> cred_metadata(mac.size());
  GetSecureRandom(cred_metadata.data(), cred_metadata.size());
  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  if (!hash_tree_->StoreLabel(label_obj, mac, cred_metadata, true)) {
    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "InsertCredentialReplay disk update "
                  "failed, label: "
               << label_obj.value();
    // TODO(crbug.com/809749): Report failure to UMA.
    return false;
  }
  ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  return true;
}

bool LECredentialManagerImpl::ReplayCheck(
    uint64_t label, const std::vector<uint8_t>& log_root) {
  LOG(INFO) << "Replaying check for label " << label;

  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  if (!RetrieveLabelInfo(label_obj, &orig_cred, &orig_mac, &h_aux,
                         &metadata_lost)
           .ok()) {
    ReportLEResult(kLEOpSync, kLEActionLoadFromDisk, LE_CRED_ERROR_HASH_TREE);
    return false;
  }

  ReportLEResult(kLEOpSync, kLEActionLoadFromDisk, LE_CRED_SUCCESS);

  std::vector<uint8_t> new_cred, new_mac;
  if (!le_tpm_backend_->ReplayLogOperation(log_root, h_aux, orig_cred,
                                           &new_cred, &new_mac)) {
    ReportLEResult(kLEOpSync, kLEActionBackendReplayLog,
                   LE_CRED_ERROR_UNCLASSIFIED);
    LOG(ERROR) << "Auth replay failed on LE Backend, label: " << label;
    // TODO(crbug.com/809749): Report failure to UMA.
    return false;
  }

  ReportLEResult(kLEOpSync, kLEActionBackendReplayLog, LE_CRED_SUCCESS);

  // Store the new credential metadata and MAC.
  if (!new_cred.empty() && !new_mac.empty()) {
    if (!hash_tree_->StoreLabel(label_obj, new_mac, new_cred, false)) {
      ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << "Error in LE auth replay disk hash tree update, label: "
                 << label;
      // TODO(crbug.com/809749): Report failure to UMA.
      return false;
    }

    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);
  }

  return true;
}

bool LECredentialManagerImpl::ReplayResetTree() {
  LOG(INFO) << "Replaying tree reset";

  hash_tree_.reset();
  if (!base::DeletePathRecursively(basedir_)) {
    PLOG(ERROR) << "Failed to delete disk hash tree during replay.";
    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    return false;
  }

  ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  auto new_hash_tree =
      std::make_unique<SignInHashTree>(kLengthLabels, kBitsPerLevel, basedir_);
  if (!new_hash_tree->IsValid()) {
    return false;
  }
  hash_tree_ = std::move(new_hash_tree);
  hash_tree_->GenerateAndStoreHashCache();
  return true;
}

bool LECredentialManagerImpl::ReplayRemove(uint64_t label) {
  LOG(INFO) << "Replaying remove for label " << label;

  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  if (!hash_tree_->RemoveLabel(label_obj)) {
    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "RemoveLabel LE Replay failed for label: " << label;
    // TODO(crbug.com/809749): Report failure to UMA.
    return false;
  }
  ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);
  return true;
}

bool LECredentialManagerImpl::ReplayLogEntries(
    const std::vector<LELogEntry>& log,
    const std::vector<uint8_t>& disk_root_hash) {
  // The log entries are in reverse chronological order. Because the log entries
  // only store the root hash after the operation, the strategy here is:
  // - Parse the logs in reverse.
  // - First try to find a log entry which matches the on-disk root hash,
  //   and start with the log entry following that. If you can't, then just
  //   start from the earliest log.
  // - For all other entries, simply attempt to replay the operation.
  auto it = log.rbegin();
  size_t replay_count = 0;
  for (; it != log.rend(); ++it) {
    const LELogEntry& log_entry = *it;
    if (log_entry.root == disk_root_hash) {
      // 1-based count, zero indicates no root hash match.
      replay_count = it - log.rbegin() + 1;
      LOG(INFO) << "Starting replay at log entry #" << it - log.rbegin();
      ++it;
      break;
    }
  }

  ReportLELogReplayEntryCount(replay_count);

  if (it == log.rend()) {
    LOG(WARNING) << "No matching root hash, starting replay at oldest entry";
    it = log.rbegin();
  }

  std::vector<uint8_t> cur_root_hash = disk_root_hash;
  std::vector<uint64_t> inserted_leaves;
  for (; it != log.rend(); ++it) {
    const LELogEntry& log_entry = *it;
    bool ret;
    switch (log_entry.type) {
      case LE_LOG_INSERT:
        ret = ReplayInsert(log_entry.label, log_entry.root, log_entry.mac);
        if (ret) {
          inserted_leaves.push_back(log_entry.label);
        }
        break;
      case LE_LOG_REMOVE:
        ret = ReplayRemove(log_entry.label);
        break;
      case LE_LOG_CHECK:
        ret = ReplayCheck(log_entry.label, log_entry.root);
        break;
      case LE_LOG_RESET:
        ret = ReplayResetTree();
        break;
      case LE_LOG_INVALID:
        LOG(ERROR) << "Invalid log entry.";
        return false;
    }
    if (!ret) {
      LOG(ERROR) << "Failure to replay LE Cred log entries.";
      return false;
    }
    cur_root_hash.clear();
    hash_tree_->GetRootHash(&cur_root_hash);
    if (cur_root_hash != log_entry.root) {
      LOG(ERROR) << "Root hash doesn't match log root after replaying entry.";
      return false;
    }
  }

  // Remove any inserted leaves since they are unusable.
  for (const auto& label : inserted_leaves) {
    if (!RemoveCredential(label).ok()) {
      LOG(ERROR) << "Failed to remove re-inserted label: " << label;
      return false;
    }
  }

  return true;
}

}  // namespace cryptohome
