// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_IMPL_H_
#define LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_IMPL_H_

#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/backend/pinweaver_manager/sign_in_hash_tree.h"
#include "libhwsec/middleware/metrics.h"
#include "libhwsec/status.h"

using CredentialTreeResult = hwsec::PinWeaver::CredentialTreeResult;
using LogEntry = hwsec::PinWeaver::GetLogResult::LogEntry;
namespace hwsec {

// Class containing all logic pertaining to management of Low Entropy(LE)
// credentials. The stated aim of this class should be the following:
// - Provide an interface to Set and Remove credentials in the underlying
// storage.
// - Provide an interface to verify a credential.
//
// This class contains a SignInHashTree object, which is used to store and
// maintain the credentials on disk.
//
// It also contains a pointer to a TPM object which will be able to invoke the
// necessary commands on the TPM side, for verification.
class PinWeaverManagerImpl : public PinWeaverManager {
 public:
  enum class UpdateHashTreeType : uint8_t {
    kInsertLeaf,
    kUpdateLeaf,
    kRemoveLeaf,
    kReplayInsertLeaf,
    kMaxValue = kReplayInsertLeaf,
  };

  PinWeaverManagerImpl(PinWeaver& pinweaver,
                       const base::FilePath& le_basedir,
                       Metrics* metrics)
      : pinweaver_(pinweaver), basedir_(le_basedir), metrics_(metrics) {}

  virtual ~PinWeaverManagerImpl() {}

  StatusOr<uint64_t> InsertCredential(
      const std::vector<hwsec::OperationPolicySetting>& policies,
      const brillo::SecureBlob& le_secret,
      const brillo::SecureBlob& he_secret,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_sched,
      std::optional<uint32_t> expiration_delay) override;

  StatusOr<CheckCredentialReply> CheckCredential(
      uint64_t label, const brillo::SecureBlob& le_secret) override;

  Status ResetCredential(uint64_t label,
                         const brillo::SecureBlob& reset_secret,
                         ResetType reset_type) override;

  Status RemoveCredential(uint64_t label) override;

  StatusOr<uint32_t> GetWrongAuthAttempts(uint64_t label) override;

  StatusOr<uint32_t> GetDelayInSeconds(uint64_t label) override;

  StatusOr<std::optional<uint32_t>> GetExpirationInSeconds(
      uint64_t label) override;

  StatusOr<DelaySchedule> GetDelaySchedule(uint64_t label) override;

  StatusOr<uint64_t> InsertRateLimiter(
      uint8_t auth_channel,
      const std::vector<hwsec::OperationPolicySetting>& policies,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_sched,
      std::optional<uint32_t> expiration_delay) override;

  StatusOr<StartBiometricsAuthReply> StartBiometricsAuth(
      uint8_t auth_channel,
      uint64_t label,
      const brillo::Blob& client_nonce) override;

  Status SyncHashTree() override;

 private:
  // Performs checks to ensure the SignInHashTree is at good state. This
  // includes insuring that PinWeaverManager is initialized, not locked out,
  // and hash tree is valid.
  // All public PW operation functions should first call StateIsReady().
  Status StateIsReady();

  // Helper to turn a label into an original credential. Helper for a lot of the
  // Get* functions which starts with a label and first need to turn it into a
  // credential to call the actual Pinweaver function they need to call.
  StatusOr<brillo::Blob> GetCredentialMetadata(uint64_t label);

  // Since the InsertCredential() and InsertRateLimiter() functions are very
  // similar, this function combines the common parts of both the calls
  // into a generic "insert leaf" function. |auth_channel| is only valid in
  // InsertRateLimiter(), while |le_secret| and |he_secret| is only valid in
  // InsertCredential(). |is_rate_limiter| is used to signal whether the leaf
  // being inserted is a rate-limiter (true) or a normal credential (false).
  //
  // The returned label should be placed into the metadata associated with the
  // authentication factor, so that it can be used to look up the credential
  // later.
  StatusOr<uint64_t> InsertLeaf(
      std::optional<uint8_t> auth_channel,
      const std::vector<hwsec::OperationPolicySetting>& policies,
      const brillo::SecureBlob* le_secret,
      const brillo::SecureBlob* he_secret,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_sched,
      std::optional<uint32_t> expiration_delay,
      bool is_rate_limiter);

  // Helper function to retrieve the credential metadata, MAC, and auxiliary
  // hashes associated with a label |label| (stored in |cred_metadata|, |mac|
  // and |h_aux| respectively). |metadata_lost| will denote whether the label
  // contains valid metadata (false) or not (true).
  Status RetrieveLabelInfo(const SignInHashTree::Label& label,
                           std::vector<brillo::Blob>& h_aux,
                           brillo::Blob& cred_metadata,
                           brillo::Blob& mac,
                           bool& metadata_lost);

  // Given a label, gets the list of auxiliary hashes for that label.
  // On failure, returns an empty vector.
  StatusOr<std::vector<brillo::Blob>> GetAuxHashes(
      const SignInHashTree::Label& label);

  // Updates the SignInHashTree to insert or update the credential with label
  // |label|. The credential meta data and MAC are provided in
  // |cred_tree_result|.
  // The |update_type| argument determines whether the credential should be
  // stored with `metadata_lost=true` and the error handling logic when
  // StoreLabel failed.
  // If the update fails, the hash tree may be locked (depending on the
  // |update_type|) to block further Pinweaver operations until at least the
  // next boot.
  Status UpdateHashTree(const SignInHashTree::Label& label,
                        const brillo::Blob* cred_metadata,
                        const brillo::Blob* mac,
                        UpdateHashTreeType update_type);
  // Replays the InsertCredential operation using the information provided
  // from the log entry from the LE credential backend.
  // |label| denotes which label to perform the operation on,
  // |log_root| is what the root hash should be after this operation is
  // completed. It should directly be used from the log entry.
  // |mac| is the MAC of the credential which has to be inserted.
  //
  // NOTE: A replayed insert is unusable and should be deleted after the replay
  // is complete.
  Status ReplayInsert(const LogEntry& log_entry);

  // Replays the CheckCredential / ResetCredential operation using the
  // information provided from the log entry from the LE credential
  // backend.
  // |label| denotes which credential label to perform the operation on.
  // |log_root| is what the root hash should be after this operation is
  // completed. It should directly be used from the log entry.
  // |is_full_replay| is whether the log_replay is done with successfully
  // locating the current root hash in the log entries, or done with replaying
  // using all entries.
  Status ReplayCheck(const LogEntry& log_entry);

  // Resets the HashTree.
  Status ReplayResetTree();

  // Replays the RemoveCredential for |label| which is provided from
  // the LE Backend Replay logs.
  Status ReplayRemove(const LogEntry& log_entry);

  // Check whether the current root hash in cache is same as |log_root|.
  // This is expected to be called in replay operations (except
  // ReplayResetRree).
  Status MatchLogRootAfterReplayOperation(const brillo::Blob& log_root);

  // Replays all the log operations provided in |log|, and makes the
  // corresponding updates to the HashTree.
  Status ReplayLogEntries(
      const std::vector<PinWeaver::GetLogResult::LogEntry>& log,
      const brillo::Blob& disk_root_hash);

  void ReportSyncOutcome(SyncOutcome result) {
    if (metrics_) {
      metrics_->SendPinWeaverSyncOutcomeToUMA(result);
    }
  }

  void ReportLogReplayResult(ReplayEntryType type, LogReplayResult result) {
    if (metrics_) {
      metrics_->SendPinWeaverLogReplayResultToUMA(type, result);
    }
  }

  void ReportReplayOperationResult(
      ReplayEntryType replay_type,
      PinWeaver::GetLogResult::LogEntryType entry_type,
      const Status& status) {
    if (metrics_) {
      metrics_->SendPinWeaverReplayOperationResultToUMA(replay_type, entry_type,
                                                        status);
    }
  }

  // Last resort flag which prevents any further Low Entropy operations from
  // occurring, till the next time the class is instantiated.
  // This is used in a situation where an operation succeeds on the TPM,
  // but its on-disk counterpart fails. In this case, the mitigation strategy
  // is as follows:
  // - Prevent any further pinweaver operations, to prevent disk and TPM from
  // going further out of state, till next reboot.
  // - Hope that on reboot, the problems causing disk failure don't recur,
  // and the TPM replay log will enable the disk state to get in sync with
  // the TPM again.
  //
  // We will collect UMA stats from the field and refine this strategy
  // as required.
  bool is_initialized_ = false;
  bool is_locked_ = false;
  // In-memory copy of LEBackend's root hash value.
  brillo::Blob root_hash_;
  // Reference of an implementation of the pinweaver operations.
  PinWeaver& pinweaver_;
  // Directory where all pinweaver credential related data is stored.
  base::FilePath basedir_;
  Metrics* metrics_;
  std::unique_ptr<SignInHashTree> hash_tree_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_IMPL_H_
