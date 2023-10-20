// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_STORAGE_BASE_H_
#define MISSIVE_STORAGE_STORAGE_BASE_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/functional/callback_forward.h>
#include <base/memory/ref_counted.h>
#include <base/memory/ref_counted_delete_on_sequence.h>
#include <base/task/sequenced_task_runner.h>
#include <base/memory/scoped_refptr.h>

#include "missive/encryption/encryption_module_interface.h"
#include "missive/encryption/verification.h"
#include "missive/health/health_module.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_queue.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"

// This file is for common logic shared in both implementations of the storage
// class: multi-generation and single-generation (legacy).
namespace reporting {

// Helper class keeps all `StorageQueue`s and manages controlled degradation
// if is is enabled. The queues are indexed by priority and generation, even
// though legacy Storage does not actually use generation.
// Note: Destruction of `Storage` will  trigger destruction of all
// `StorageQueues` inside `QueuesContainer`, but may not destroy
// `QueuesContainer` itself since components besides `Storage` may hold the
// references to `QueuesContainer`. Destruction of `QueuesContainer` will happen
// when its reference count reaches zero.
class QueuesContainer
    : public DynamicFlag,
      public base::RefCountedDeleteOnSequence<QueuesContainer> {
 public:
  // Factory method creates task runner and the container.
  static scoped_refptr<QueuesContainer> Create(
      bool storage_degradation_enabled);
  QueuesContainer(const QueuesContainer&) = delete;
  QueuesContainer& operator=(const QueuesContainer) = delete;

  Status AddQueue(Priority priority, scoped_refptr<StorageQueue> queue);

  // Helper method that selects queue by priority. Returns error
  // if priority does not match any queue.
  StatusOr<scoped_refptr<StorageQueue>> GetQueue(
      Priority priority, GenerationGuid generation_guid) const;

  // Helper method that enumerates all queue with given priority and runs action
  // on each. Returns total count of found queues.
  size_t RunActionOnAllQueues(
      Priority priority,
      base::RepeatingCallback<void(scoped_refptr<StorageQueue>)> action) const;

  // Asynchronously constructs references to all storage queues to consider
  // for degradation for the sake of the current `queue` (candidates queue is
  // empty if degradation is disabled). The candidate queues are ordered from
  // lowest priority to the one below the current one. The method is made
  // `static` so that even if weak pointer is null, we still can respond (with
  // an empty result).
  static void GetDegradationCandidates(
      base::WeakPtr<QueuesContainer> container,
      Priority priority,
      const scoped_refptr<StorageQueue> queue,
      base::OnceCallback<void(std::queue<scoped_refptr<StorageQueue>>)>
          result_cb);

  void RegisterCompletionCallback(base::OnceClosure callback);

  base::WeakPtr<QueuesContainer> GetWeakPtr();

  // Accessors.
  bool storage_degradation_enabled() const;
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner() const;

 protected:
  ~QueuesContainer() override;

 private:
  friend base::RefCountedDeleteOnSequence<QueuesContainer>;
  friend class base::DeleteHelper<QueuesContainer>;

  // Map used to retrieve queues for writes, confirms, and flushes.
  struct Hash {
    size_t operator()(
        const std::tuple<Priority, GenerationGuid>& v) const noexcept {
      const auto& [priority, guid] = v;
      static constexpr std::hash<Priority> priority_hasher;
      static constexpr std::hash<GenerationGuid> guid_hasher;
      return priority_hasher(priority) ^ guid_hasher(guid);
    }
  };

  using QueuesMap = std::unordered_map<std::tuple<Priority, GenerationGuid>,
                                       scoped_refptr<StorageQueue>,
                                       Hash>;

  QueuesContainer(
      bool storage_degradation_enabled,
      scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner);

  const scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  QueuesMap queues_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Weak ptr factory.
  base::WeakPtrFactory<QueuesContainer> weak_ptr_factory_{this};
};

// Bridge class for uploading records from a queue to storage.
class QueueUploaderInterface : public UploaderInterface {
 public:
  QueueUploaderInterface(
      Priority priority,
      HealthModule::Recorder recorder,
      std::unique_ptr<UploaderInterface> storage_uploader_interface);

  // Factory method.
  static void AsyncProvideUploader(
      Priority priority,
      const scoped_refptr<HealthModule> health_module,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      UploaderInterface::UploadReason reason,
      UploaderInterfaceResultCb start_uploader_cb);

  void ProcessRecord(EncryptedRecord encrypted_record,
                     ScopedReservation scoped_reservation,
                     base::OnceCallback<void(bool)> processed_cb) override;

  void ProcessGap(SequenceInformation start,
                  uint64_t count,
                  base::OnceCallback<void(bool)> processed_cb) override;

  void Completed(Status final_status) override;

 private:
  static void WrapInstantiatedUploader(
      Priority priority,
      HealthModule::Recorder recorder,
      UploaderInterfaceResultCb start_uploader_cb,
      StatusOr<std::unique_ptr<UploaderInterface>> uploader_result);

  const Priority priority_;
  HealthModule::Recorder recorder_;
  const std::unique_ptr<UploaderInterface> storage_uploader_interface_;
};

// Class for key upload/download to the file system in storage.
class KeyDelivery {
 public:
  using RequestCallback = base::OnceCallback<void(Status)>;

  // Factory method, returns smart pointer with deletion on sequence.
  static std::unique_ptr<KeyDelivery, base::OnTaskRunnerDeleter> Create(
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      scoped_refptr<HealthModule> health_module,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb);

  ~KeyDelivery();

  void Request(RequestCallback callback);

  void OnCompletion(Status status);

  void StartPeriodicKeyUpdate(const base::TimeDelta period);

 private:
  // Constructor called by factory only.
  explicit KeyDelivery(
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      scoped_refptr<HealthModule> health_module,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
      scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner);

  void RequestKeyIfNeeded();

  void EuqueueRequestAndPossiblyStart(HealthModule::Recorder recorder,
                                      RequestCallback callback);

  void PostResponses(Status status);

  static void WrapInstantiatedKeyUploader(
      Priority priority,
      HealthModule::Recorder recorder,
      UploaderInterface::UploaderInterfaceResultCb start_uploader_cb,
      StatusOr<std::unique_ptr<UploaderInterface>> uploader_result);

  void EncryptionKeyReceiverReady(
      StatusOr<std::unique_ptr<UploaderInterface>> uploader_result);

  const scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  // Upload provider callback.
  const UploaderInterface::AsyncStartUploaderCb async_start_upload_cb_;

  // List of all request callbacks.
  std::vector<RequestCallback> callbacks_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Used to check whether or not encryption is enabled and if we need to
  // request the key.
  const scoped_refptr<EncryptionModuleInterface> encryption_module_;

  // Used for recording key delivery upload call, when debugging is enabled.
  const scoped_refptr<HealthModule> health_module_;

  // Used to periodically trigger check for encryption key
  base::RepeatingTimer upload_timer_ GUARDED_BY_CONTEXT(sequence_checker_);
};

// Class that represents the encryption key in storage.
class KeyInStorage {
 public:
  KeyInStorage(std::string_view signature_verification_public_key,
               scoped_refptr<SignatureVerificationDevFlag>
                   signature_verification_dev_flag,
               const base::FilePath& directory);
  ~KeyInStorage();

  // Uploads signed encryption key to a file with an |index| >=
  // |next_key_file_index_|. Returns status in case of any error. If succeeds,
  // removes all files with lower indexes (if any). Called every time encryption
  // key is updated.
  Status UploadKeyFile(const SignedEncryptionInfo& signed_encryption_key);

  // Locates and downloads the latest valid enumeration keys file.
  // Atomically sets |next_key_file_index_| to the a value larger than any found
  // file. Returns key and key id pair, or error status (NOT_FOUND if no valid
  // file has been found). Called once during initialization only.
  StatusOr<std::pair<std::string, EncryptionModuleInterface::PublicKeyId>>
  DownloadKeyFile();

  Status VerifySignature(const SignedEncryptionInfo& signed_encryption_key);

 private:
  // Writes key into file. Called during key upload.
  Status WriteKeyInfoFile(uint64_t new_file_index,
                          const SignedEncryptionInfo& signed_encryption_key);

  // Enumerates key files and deletes those with index lower than
  // |new_file_index|. Called during key upload.
  void RemoveKeyFilesWithLowerIndexes(uint64_t new_file_index);

  // Enumerates possible key files, collects the ones that have valid name,
  // sets next_key_file_index_ to a value that is definitely not used.
  // Called once, during initialization.
  void EnumerateKeyFiles(
      std::unordered_set<base::FilePath>* all_key_files,
      std::map<uint64_t, base::FilePath, std::greater<>>* found_key_files);

  // Enumerates found key files and locates one with the highest index and
  // valid key. Returns pair of file name and loaded signed key proto.
  // Called once, during initialization.
  std::optional<std::pair<base::FilePath, SignedEncryptionInfo>>
  LocateValidKeyAndParse(
      const std::map<uint64_t, base::FilePath, std::greater<>>&
          found_key_files);

  // Index of the file to serialize the signed key to.
  // Initialized to the next available number or 0, if none present.
  // Every time a new key is received, it is stored in a file with the next
  // index; however, any file found with the matching signature can be used
  // to successfully encrypt records and for the server to then decrypt them.
  std::atomic<uint64_t> next_key_file_index_{0};

  const SignatureVerifier verifier_;

  const base::FilePath directory_;
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_STORAGE_BASE_H_
