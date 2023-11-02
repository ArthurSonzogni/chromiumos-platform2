// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/pinweaver_manager/pinweaver_manager_impl.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/files/file_util.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/backend/pinweaver_manager/sign_in_hash_tree.h"
#include "libhwsec/backend/pinweaver_manager/sync_hash_tree_types.h"
#include "libhwsec/error/pinweaver_error.h"
#include "libhwsec/error/tpm_error.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/status.h"

namespace {

// Constants used to define the hash tree.
constexpr uint32_t kLengthLabels = 14;
constexpr uint32_t kBitsPerLevel = 2;

}  // namespace

using CredentialTreeResult = hwsec::PinWeaver::CredentialTreeResult;
using GetLogResult = hwsec::PinWeaver::GetLogResult;
using hwsec_foundation::GetSecureRandom;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;

namespace hwsec {

using PinWeaverErrorCode = PinWeaver::CredentialTreeResult::ErrorCode;

Status PinWeaverManagerImpl::StateIsReady() {
  if (is_initialized_) {
    if (!hash_tree_->IsValid()) {
      return MakeStatus<TPMError>("Invalid hash tree",
                                  TPMRetryAction::kNoRetry);
    }
    if (is_locked_) {
      return MakeStatus<TPMError>(
          "PinWeaver Manager locked due to"
          "previous failing disk update",
          TPMRetryAction::kReboot);
    }

    return OkStatus();
  }

  ASSIGN_OR_RETURN(bool is_enabled, pinweaver_.IsEnabled());
  if (!is_enabled) {
    return MakeStatus<TPMError>("Pinweaver Unsupported",
                                TPMRetryAction::kNoRetry);
  }

  // Check if hash tree already exists.
  bool new_hash_tree = !base::PathExists(basedir_);

  hash_tree_ =
      std::make_unique<SignInHashTree>(kLengthLabels, kBitsPerLevel, basedir_);
  if (!hash_tree_->IsValid()) {
    return MakeStatus<TPMError>(
        "Failed to initialize pinweaver credential manager:"
        "invalid hash tree",
        TPMRetryAction::kNoRetry);
  }

  // Reset the root hash in the TPM to its initial value.
  if (new_hash_tree) {
    ASSIGN_OR_RETURN(const CredentialTreeResult& result,
                     pinweaver_.Reset(kBitsPerLevel, kLengthLabels));
    root_hash_ = result.new_root;
    hash_tree_->GenerateAndStoreHashCache();
  } else {
    // Since we're using mmap leaf cache, we don't need to read all leaf data
    // from PLT(disk). Here we still need to generates the inner hash array
    // (since it's not stored on disk).
    hash_tree_->GenerateInnerHashArray();
  }

  is_initialized_ = true;
  return OkStatus();
}

StatusOr<uint64_t> PinWeaverManagerImpl::InsertCredential(
    const std::vector<hwsec::OperationPolicySetting>& policies,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const PinWeaverManager::DelaySchedule& delay_sched,
    std::optional<uint32_t> expiration_delay) {
  RETURN_IF_ERROR(StateIsReady());
  return InsertLeaf(std::nullopt, policies, &le_secret, &he_secret,
                    reset_secret, delay_sched, expiration_delay,
                    /*is_rate_limiter=*/false);
}

StatusOr<PinWeaverManagerImpl::CheckCredentialReply>
PinWeaverManagerImpl::CheckCredential(uint64_t label,
                                      const brillo::SecureBlob& le_secret) {
  RETURN_IF_ERROR(StateIsReady());

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  brillo::Blob orig_cred, orig_mac;
  std::vector<brillo::Blob> h_aux;
  bool metadata_lost;
  RETURN_IF_ERROR(RetrieveLabelInfo(label_object, h_aux, orig_cred, orig_mac,
                                    metadata_lost));

  if (metadata_lost) {
    return MakeStatus<TPMError>(
        "Invalid cred metadata for label: " + std::to_string(label),
        TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      const CredentialTreeResult& result,
      pinweaver_.CheckCredential(label, h_aux, orig_cred, le_secret));
  root_hash_ = result.new_root;
  if (result.new_cred_metadata.has_value() && result.new_mac.has_value()) {
    RETURN_IF_ERROR(UpdateHashTree(
        label_object, &result.new_cred_metadata.value(),
        &result.new_mac.value(), UpdateHashTreeType::kUpdateLeaf));
  }

  RETURN_IF_ERROR(MakeStatus<PinWeaverError>(result.error));
  return PinWeaverManager::CheckCredentialReply{
      .he_secret = result.he_secret.value(),
      .reset_secret = result.reset_secret.value(),
  };
}

Status PinWeaverManagerImpl::ResetCredential(
    uint64_t label,
    const brillo::SecureBlob& reset_secret,
    ResetType reset_type) {
  RETURN_IF_ERROR(StateIsReady());

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  brillo::Blob orig_cred, orig_mac;
  std::vector<brillo::Blob> h_aux;
  bool metadata_lost;
  RETURN_IF_ERROR(RetrieveLabelInfo(label_object, h_aux, orig_cred, orig_mac,
                                    metadata_lost));

  if (metadata_lost) {
    return MakeStatus<TPMError>(
        "Invalid cred metadata for label: " + std::to_string(label),
        TPMRetryAction::kNoRetry);
  }

  bool strong_reset = reset_type == ResetType::kWrongAttemptsAndExpirationTime;
  ASSIGN_OR_RETURN(const CredentialTreeResult& result,
                   pinweaver_.ResetCredential(label, h_aux, orig_cred,
                                              reset_secret, strong_reset));
  root_hash_ = result.new_root;

  if (result.new_cred_metadata.has_value() && result.new_mac.has_value()) {
    RETURN_IF_ERROR(UpdateHashTree(
        label_object, &result.new_cred_metadata.value(),
        &result.new_mac.value(), UpdateHashTreeType::kUpdateLeaf));
  }
  return MakeStatus<PinWeaverError>(result.error);
}

Status PinWeaverManagerImpl::RemoveCredential(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);
  brillo::Blob orig_cred, orig_mac;
  std::vector<brillo::Blob> h_aux;
  bool metadata_lost;

  RETURN_IF_ERROR(RetrieveLabelInfo(label_object, h_aux, orig_cred, orig_mac,
                                    metadata_lost));

  ASSIGN_OR_RETURN(const CredentialTreeResult& result,
                   pinweaver_.RemoveCredential(label, h_aux, orig_mac));
  root_hash_ = result.new_root;

  RETURN_IF_ERROR(UpdateHashTree(label_object, nullptr, nullptr,
                                 UpdateHashTreeType::kRemoveLeaf));
  return OkStatus();
}

StatusOr<uint32_t> PinWeaverManagerImpl::GetWrongAuthAttempts(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  brillo::Blob orig_cred, orig_mac;
  std::vector<brillo::Blob> h_aux;
  bool metadata_lost;

  RETURN_IF_ERROR(RetrieveLabelInfo(label_object, h_aux, orig_cred, orig_mac,
                                    metadata_lost));
  return pinweaver_.GetWrongAuthAttempts(orig_cred);
}

StatusOr<uint32_t> PinWeaverManagerImpl::GetDelayInSeconds(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  ASSIGN_OR_RETURN(const brillo::Blob& metadata, GetCredentialMetadata(label));
  return pinweaver_.GetDelayInSeconds(metadata);
}

StatusOr<std::optional<uint32_t>> PinWeaverManagerImpl::GetExpirationInSeconds(
    uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  ASSIGN_OR_RETURN(const brillo::Blob& metadata, GetCredentialMetadata(label));
  return pinweaver_.GetExpirationInSeconds(metadata);
}

StatusOr<PinWeaverManagerImpl::DelaySchedule>
PinWeaverManagerImpl::GetDelaySchedule(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  ASSIGN_OR_RETURN(const brillo::Blob& metadata, GetCredentialMetadata(label));
  return pinweaver_.GetDelaySchedule(metadata);
}

StatusOr<uint64_t> PinWeaverManagerImpl::InsertRateLimiter(
    uint8_t auth_channel,
    const std::vector<hwsec::OperationPolicySetting>& policies,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_sched,
    std::optional<uint32_t> expiration_delay) {
  RETURN_IF_ERROR(StateIsReady());
  return InsertLeaf(auth_channel, policies, nullptr, nullptr, reset_secret,
                    delay_sched, expiration_delay,
                    /*is_rate_limiter=*/true);
}

StatusOr<PinWeaverManagerImpl::StartBiometricsAuthReply>
PinWeaverManagerImpl::StartBiometricsAuth(uint8_t auth_channel,
                                          uint64_t label,
                                          const brillo::Blob& client_nonce) {
  RETURN_IF_ERROR(StateIsReady());

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);
  brillo::Blob orig_cred, orig_mac;
  std::vector<brillo::Blob> h_aux;
  bool metadata_lost;
  RETURN_IF_ERROR(RetrieveLabelInfo(label_object, h_aux, orig_cred, orig_mac,
                                    metadata_lost));
  if (metadata_lost) {
    return MakeStatus<TPMError>(
        "Invalid cred metadata for label: " + std::to_string(label),
        TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(const CredentialTreeResult& result,
                   pinweaver_.StartBiometricsAuth(auth_channel, label, h_aux,
                                                  orig_cred, client_nonce));

  root_hash_ = result.new_root;

  if (result.new_cred_metadata.has_value() && result.new_mac.has_value()) {
    RETURN_IF_ERROR(UpdateHashTree(
        label_object, &result.new_cred_metadata.value(),
        &result.new_mac.value(), UpdateHashTreeType::kUpdateLeaf));
  }
  RETURN_IF_ERROR(MakeStatus<PinWeaverError>(result.error));

  if (!result.server_nonce.has_value() || !result.iv.has_value() ||
      !result.encrypted_he_secret.has_value()) {
    return MakeStatus<TPMError>("Invalid output for StartBiometricsAuth",
                                TPMRetryAction::kNoRetry);
  }

  PinWeaverManager::StartBiometricsAuthReply reply{
      .server_nonce = std::move(result.server_nonce.value()),
      .iv = std::move(result.iv.value()),
      .encrypted_he_secret = std::move(result.encrypted_he_secret.value()),
  };

  return reply;
}

Status PinWeaverManagerImpl::SyncHashTree() {
  if (Status status = StateIsReady(); !status.ok()) {
    ReportSyncOutcome(SyncOutcome::kStateNotReady);
    return MakeStatus<TPMError>(
               "Attempted to SyncHashTree but state isn't ready")
        .Wrap(std::move(status));
  }

  brillo::Blob disk_root_hash;
  LOG(WARNING)
      << "PinWeaver HashCache is stale; reconstruct the hash tree locally.";
  hash_tree_->GenerateAndStoreHashCache();
  hash_tree_->GetRootHash(disk_root_hash);

  // If we don't have the root hash, get it by sending the PW GetLog command.
  if (root_hash_.empty()) {
    hwsec::StatusOr<GetLogResult> result = pinweaver_.GetLog(disk_root_hash);
    if (!result.ok()) {
      is_locked_ = true;
      ReportSyncOutcome(SyncOutcome::kGetLogFailed);
      return MakeStatus<TPMError>("Couldn't get pinweaver log from GSC")
          .Wrap(std::move(result).err_status());
    }
    root_hash_ = result->root_hash;
  }

  if (disk_root_hash == root_hash_) {
    ReportSyncOutcome(SyncOutcome::kSuccessAfterLocalReconstruct);
    return OkStatus();
  }

  // Get the log again, since |disk_root_hash| may have changed.
  std::vector<GetLogResult::LogEntry> log;
  hwsec::StatusOr<GetLogResult> result = pinweaver_.GetLog(disk_root_hash);
  if (!result.ok()) {
    is_locked_ = true;
    ReportSyncOutcome(SyncOutcome::kGetLogFailed);
    return MakeStatus<TPMError>("Couldn't get pinweaver log from GSC")
        .Wrap(std::move(result).err_status());
  }
  root_hash_ = result->root_hash;
  log = std::move(result->log_entries);

  LOG(WARNING) << "PinWeaver hash tree sync loss between OS and GSC, "
                  "attempting log replay.";
  ReportSyncOutcome(SyncOutcome::kLogReplay);

  Status replay_result = ReplayLogEntries(log, disk_root_hash);
  if (!replay_result.ok()) {
    is_locked_ = true;
    return MakeStatus<TPMError>("Replay log failed")
        .Wrap(std::move(replay_result).err_status());
  }
  return OkStatus();
}

StatusOr<brillo::Blob> PinWeaverManagerImpl::GetCredentialMetadata(
    uint64_t label) {
  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  brillo::Blob orig_cred, orig_mac;
  std::vector<brillo::Blob> h_aux;
  bool metadata_lost;

  RETURN_IF_ERROR(RetrieveLabelInfo(label_object, h_aux, orig_cred, orig_mac,
                                    metadata_lost));

  if (metadata_lost) {
    return MakeStatus<TPMError>(
        "Invalid cred metadata for label: " + std::to_string(label),
        TPMRetryAction::kNoRetry);
  }
  return orig_cred;
}

StatusOr<uint64_t> PinWeaverManagerImpl::InsertLeaf(
    std::optional<uint8_t> auth_channel,
    const std::vector<hwsec::OperationPolicySetting>& policies,
    const brillo::SecureBlob* le_secret,
    const brillo::SecureBlob* he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_sched,
    std::optional<uint32_t> expiration_delay,
    bool is_rate_limiter) {
  if ((is_rate_limiter && !auth_channel.has_value()) ||
      (!is_rate_limiter && (!le_secret || !he_secret))) {
    return MakeStatus<TPMError>("Invalid input parameters",
                                TPMRetryAction::kNoRetry);
  }

  std::optional<SignInHashTree::Label> label_opt = hash_tree_->GetFreeLabel();
  if (!label_opt.has_value()) {
    return MakeStatus<TPMError>("No free labels available",
                                TPMRetryAction::kSpaceNotFound);
  }
  const auto& label = label_opt.value();
  ASSIGN_OR_RETURN(const std::vector<brillo::Blob>& h_aux, GetAuxHashes(label));
  ASSIGN_OR_RETURN(
      const CredentialTreeResult& result,
      is_rate_limiter
          ? pinweaver_.InsertRateLimiter(*auth_channel, policies, label.value(),
                                         h_aux, reset_secret, delay_sched,
                                         expiration_delay)
          : pinweaver_.InsertCredential(policies, label.value(), h_aux,
                                        *le_secret, *he_secret, reset_secret,
                                        delay_sched, expiration_delay));
  root_hash_ = result.new_root;
  RETURN_IF_ERROR(UpdateHashTree(label, &result.new_cred_metadata.value(),
                                 &result.new_mac.value(),
                                 UpdateHashTreeType::kInsertLeaf));
  return label.value();
}

Status PinWeaverManagerImpl::RetrieveLabelInfo(
    const SignInHashTree::Label& label,
    std::vector<brillo::Blob>& h_aux,
    brillo::Blob& cred_metadata,
    brillo::Blob& mac,
    bool& metadata_lost) {
  if (!hash_tree_->GetLabelData(label, mac, cred_metadata, metadata_lost)) {
    return MakeStatus<TPMError>(
        "Failed to get the credential in disk hash tree for label: " +
            std::to_string(label.value()),
        TPMRetryAction::kSpaceNotFound);
  }

  // Any empty |cred_metadata| means the label isn't present in the hash tree.
  if (cred_metadata.empty()) {
    return MakeStatus<TPMError>(
        "Label doesn't exist in hash tree: " + std::to_string(label.value()),
        TPMRetryAction::kSpaceNotFound);
  }

  ASSIGN_OR_RETURN(h_aux, GetAuxHashes(label));
  return OkStatus();
}

StatusOr<std::vector<brillo::Blob>> PinWeaverManagerImpl::GetAuxHashes(
    const SignInHashTree::Label& label) {
  std::vector<SignInHashTree::Label> aux_labels =
      hash_tree_->GetAuxiliaryLabels(label);
  std::vector<brillo::Blob> h_aux;
  if (aux_labels.empty()) {
    return MakeStatus<TPMError>(
        "Error getting h_aux for label:" + std::to_string(label.value()),
        TPMRetryAction::kSpaceNotFound);
  }

  h_aux.reserve(aux_labels.size());
  for (const auto& cur_aux_label : aux_labels) {
    brillo::Blob hash, cred_data;
    bool metadata_lost;
    if (!hash_tree_->GetLabelData(cur_aux_label, hash, cred_data,
                                  metadata_lost)) {
      return MakeStatus<TPMError>(
          "Error getting aux label :" + std::to_string(cur_aux_label.value()) +
              "for label: " + std::to_string(label.value()),
          TPMRetryAction::kSpaceNotFound);
    }
    h_aux.push_back(std::move(hash));
  }

  return h_aux;
}

Status PinWeaverManagerImpl::UpdateHashTree(const SignInHashTree::Label& label,
                                            const brillo::Blob* cred_metadata,
                                            const brillo::Blob* mac,
                                            UpdateHashTreeType update_type) {
  // Store the new credential meta data and MAC in case the backend performed a
  // state change. Note that this might also be needed for some failure cases.
  if (update_type == UpdateHashTreeType::kRemoveLeaf) {
    if (hash_tree_->RemoveLabel(label)) {
      return OkStatus();
    }
  } else {
    if (!cred_metadata || !mac) {
      return MakeStatus<TPMError>("Invalid input parameters",
                                  TPMRetryAction::kNoRetry);
    }
    bool metadata_lost = update_type == UpdateHashTreeType::kReplayInsertLeaf;
    if (hash_tree_->StoreLabel(label, *mac, *cred_metadata, metadata_lost)) {
      return OkStatus();
    }
  }

  // The insert into the disk hash tree failed. We have different error handling
  // according to |update_type|:
  switch (update_type) {
    case UpdateHashTreeType::kReplayInsertLeaf:
    case UpdateHashTreeType::kUpdateLeaf:
    case UpdateHashTreeType::kRemoveLeaf:
      // This is an un-salvageable state. We can't make pinweaver updates
      // anymore, since the disk state can't be updated. We block further
      // pinweaver operations until at least the next boot. The hope is that on
      // reboot, the disk operations start working. In that case, we will be
      // able to replay this operation from the TPM log.
      is_locked_ = true;
      return MakeStatus<TPMError>(
          "Failed to update credential in disk hash tree for label: " +
              std::to_string(label.value()),
          TPMRetryAction::kReboot);
    case UpdateHashTreeType::kInsertLeaf:
      // For kAddLeaf cases, we first try to remove the credential from the TPM
      // state so that we are back to where we started.
      ASSIGN_OR_RETURN(const std::vector<brillo::Blob>& h_aux,
                       GetAuxHashes(label));
      hwsec::StatusOr<CredentialTreeResult> remove_result =
          pinweaver_.RemoveCredential(label.value(), h_aux, *mac);
      if (!remove_result.ok()) {
        // The attempt to undo the PinWeaver side operation has also failed,
        // Can't do much else now. We block further pinweaver operations until
        // at least the next boot.
        is_locked_ = true;
        return MakeStatus<TPMError>(
                   "Failed to rewind aborted InsertCredential in PinWeaver, "
                   "label: " +
                   std::to_string(label.value()))
            .Wrap(std::move(remove_result).status().HintNotOk());
      } else {
        root_hash_ = remove_result->new_root;
        return MakeStatus<TPMError>(
            "InsertCredential succeeded in PinWeaver but disk update failed, "
            "label: " +
                std::to_string(label.value()),
            TPMRetryAction::kReboot);
      }
  }
}

Status PinWeaverManagerImpl::ReplayInsert(const LogEntry& log_entry) {
  const uint64_t& label = log_entry.label;
  const brillo::Blob& mac = log_entry.mac;
  LOG(INFO) << "Replaying insert for label " << label;

  // Fill cred_metadata with some random data since LECredentialManager
  // considers empty cred_metadata as a non-existent label.
  brillo::Blob cred_metadata(mac.size());
  GetSecureRandom(cred_metadata.data(), cred_metadata.size());
  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);

  RETURN_IF_ERROR(UpdateHashTree(label_obj, &cred_metadata, &mac,
                                 UpdateHashTreeType::kReplayInsertLeaf))
      .WithStatus<TPMError>(
          "InsertCredentialReplay disk update failed, label: " +
          std::to_string(label));
  return MatchLogRootAfterReplayOperation(log_entry.root);
}

Status PinWeaverManagerImpl::ReplayCheck(const LogEntry& log_entry) {
  const uint64_t& label = log_entry.label;
  const brillo::Blob& log_root = log_entry.root;
  LOG(INFO) << "Replaying check for label " << label;

  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  brillo::Blob orig_cred, orig_mac;
  std::vector<brillo::Blob> h_aux;
  bool metadata_lost;
  RETURN_IF_ERROR(
      RetrieveLabelInfo(label_obj, h_aux, orig_cred, orig_mac, metadata_lost));

  ASSIGN_OR_RETURN(hwsec::StatusOr<PinWeaver::ReplayLogOperationResult> result,
                   pinweaver_.ReplayLogOperation(log_root, h_aux, orig_cred),
                   _.WithStatus<TPMError>(
                       "Auth replay failed on pinweaver backend(GSC), label: " +
                       std::to_string(label)));

  // Store the new credential metadata and MAC.
  if (!result->new_cred_metadata.empty() && !result->new_mac.empty()) {
    RETURN_IF_ERROR(UpdateHashTree(label_obj, &result->new_cred_metadata,
                                   &result->new_mac,
                                   UpdateHashTreeType::kUpdateLeaf))
        .WithStatus<TPMError>(
            "Error in pinweaver auth replay disk hash tree update, label: " +
            std::to_string(label));
  }

  return MatchLogRootAfterReplayOperation(log_root);
}

Status PinWeaverManagerImpl::ReplayResetTree() {
  LOG(INFO) << "Replaying tree reset";

  hash_tree_.reset();
  if (!brillo::DeletePathRecursively(basedir_)) {
    return MakeStatus<TPMError>("Failed to delete disk hash tree during replay",
                                TPMRetryAction::kReboot);
  }

  auto new_hash_tree =
      std::make_unique<SignInHashTree>(kLengthLabels, kBitsPerLevel, basedir_);
  if (!new_hash_tree->IsValid()) {
    return MakeStatus<TPMError>(
        "Failed to initialize pinweaver credential manager:"
        "invalid hash tree",
        TPMRetryAction::kNoRetry);
  }

  hash_tree_ = std::move(new_hash_tree);
  hash_tree_->GenerateAndStoreHashCache();
  return OkStatus();
}

Status PinWeaverManagerImpl::ReplayRemove(const LogEntry& log_entry) {
  const uint64_t& label = log_entry.label;
  LOG(INFO) << "Replaying remove for label " << label;

  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  brillo::Blob cred_metadata, mac;
  RETURN_IF_ERROR(UpdateHashTree(label_obj, nullptr, nullptr,
                                 UpdateHashTreeType::kRemoveLeaf))
      .WithStatus<TPMError>("RemoveLabel Replay failed for label: " +
                            std::to_string(label));
  return MatchLogRootAfterReplayOperation(log_entry.root);
}

Status PinWeaverManagerImpl::MatchLogRootAfterReplayOperation(
    const brillo::Blob& log_root) {
  brillo::Blob cur_root_hash;
  hash_tree_->GetRootHash(cur_root_hash);
  if (cur_root_hash != log_root) {
    return MakeStatus<TPMError>(
        "Root hash doesn't match log root after replaying entry",
        TPMRetryAction::kNoRetry);
  }
  return OkStatus();
}

Status PinWeaverManagerImpl::ReplayLogEntries(
    const std::vector<GetLogResult::LogEntry>& log,
    const brillo::Blob& disk_root_hash) {
  // The log entries are in reverse chronological order. Because the log entries
  // only store the root hash after the operation, the strategy here is:
  // - Parse the logs in reverse.
  // - First try to find a log entry which matches the on-disk root hash,
  //   and start with the log entry following that. If you can't, then just
  //   start from the earliest log.
  // - For all other entries, simply attempt to replay the operation.
  auto it = log.rbegin();
  for (; it != log.rend(); ++it) {
    const GetLogResult::LogEntry& log_entry = *it;
    if (log_entry.root == disk_root_hash) {
      // 1-based count, zero indicates no root hash match.
      LOG(INFO) << "Starting replay at log entry #" << it - log.rbegin();
      ++it;
      break;
    }
  }

  ReplayEntryType replay_type(ReplayEntryType::kNormal);
  if (it == log.rend()) {
    LOG(WARNING) << "No matching root hash, starting replay at oldest entry";
    it = log.rbegin();
    replay_type = ReplayEntryType::kMismatchedHash;
  }

  std::vector<uint64_t> inserted_leaves;
  for (; it != log.rend(); ++it) {
    const GetLogResult::LogEntry& log_entry = *it;
    Status ret;
    switch (log_entry.type) {
      case GetLogResult::LogEntryType::kInsert:
        ret = ReplayInsert(log_entry);
        if (ret.ok()) {
          inserted_leaves.push_back(log_entry.label);
        }
        break;
      case GetLogResult::LogEntryType::kRemove:
        ret = ReplayRemove(log_entry);
        break;
      case GetLogResult::LogEntryType::kCheck:
        ret = ReplayCheck(log_entry);
        break;
      case GetLogResult::LogEntryType::kReset:
        ret = ReplayResetTree();
        break;
      case GetLogResult::LogEntryType::kInvalid:
        ReportLogReplayResult(replay_type, LogReplayResult::kInvalidLogEntry);
        return MakeStatus<TPMError>("Invalid log entry from GSC",
                                    TPMRetryAction::kNoRetry);
    }
    ReportReplayOperationResult(replay_type, log_entry.type, ret);
    if (!ret.ok()) {
      ReportLogReplayResult(replay_type, LogReplayResult::kOperationFailed);
      return MakeStatus<TPMError>("Failure to replay pinweaver log entries")
          .Wrap(std::move(ret));
    }
    // Update the replay_type for the following entry. Note that currently GSC
    // only has two entries.
    if (replay_type == ReplayEntryType::kMismatchedHash) {
      replay_type = ReplayEntryType::kSecondEntry;
    }
  }

  // Remove any inserted leaves since they are unusable.
  for (const auto& label : inserted_leaves) {
    if (Status ret = RemoveCredential(label); !ret.ok()) {
      ReportLogReplayResult(replay_type,
                            LogReplayResult::kRemoveInsertedCredentialsError);
      return MakeStatus<TPMError>("Failed to remove re-inserted label: " +
                                  std::to_string(label))
          .Wrap(std::move(ret));
    }
  }

  ReportLogReplayResult(replay_type, LogReplayResult::kSuccess);
  return OkStatus();
}

}  // namespace hwsec
