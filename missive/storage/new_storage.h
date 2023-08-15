// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_NEW_STORAGE_H_
#define MISSIVE_STORAGE_NEW_STORAGE_H_

#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <base/thread_annotations.h>
#include <base/time/time.h>

#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/encryption/verification.h"
#include "missive/health/health_module.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_base.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_queue.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

// Storage allows for multiple generations for a given priority (if
// multi-genetation mode is enabled for this priority via finch flag).

// In multi-generation mode each queue is uniquely identifiable by a generation
// globally unique ID (guid) + priority tuple The generation guid is a randomly
// generation string. Generation guids have a one-to-one relationship with <DM
// token, Priority> tuples.

// Queues are created lazily with given priority when Write is called with a
// DM token we haven't seen before, as opposed to creating all queues during
// storage creation.

// Multi-generation queue directory names now have the format of
// <priority>.<generation GUID>, as oppsed to legacy queues named just
// <priority>

// Storage only creates queues on startup if it finds non-empty queue
// subdirectories in the storage directory. But these queues do not enqueue
// new records. They send their records and stay empty until they are deleted
// on the next restart of Storage.

// Empty subdirectories in the storage directory are deleted on storage
// creation. TODO(b/278620137): should also delete empty directories every 1-2
// days.

// In single-generation mode (legacy mode) there is only one queue per priority.
// Queues are created at the first start of the Storage and never erased.

class Storage : public base::RefCountedThreadSafe<Storage> {
 public:
  // Transient settings used by `Storage` instantiation.
  struct Settings {
    const StorageOptions& options;
    const scoped_refptr<QueuesContainer> queues_container;
    const scoped_refptr<EncryptionModuleInterface> encryption_module;
    const scoped_refptr<CompressionModule> compression_module;
    const scoped_refptr<SignatureVerificationDevFlag>
        signature_verification_dev_flag;
    const UploaderInterface::AsyncStartUploaderCb async_start_upload_cb;
  };

  // Creates Storage instance and returns it with the completion callback.
  static void Create(
      const Settings& settings,
      base::OnceCallback<void(StatusOr<scoped_refptr<Storage>>)> completion_cb);

  // Wraps and serializes Record (taking ownership of it), encrypts and writes
  // the resulting blob into the Storage (the last file of it) according to
  // the priority with the next sequencing id assigned. If file is going to
  // become too large, it is closed and new file is created.
  void Write(Priority priority,
             Record record,
             base::OnceCallback<void(Status)> completion_cb);

  // Confirms acceptance of the records according to the
  // |sequence_information.priority()| up to
  // |sequence_information.sequencing_id()| (inclusively), if the
  // |sequence_information.generation_id()| matches. All records with sequencing
  // ids <= this one can be removed from the Storage, and can no longer be
  // uploaded. In order to reset to the very first record (seq_id=0)
  // |sequence_information.sequencing_id()| should be set to -1.
  // If |force| is false (which is used in most cases),
  // |sequence_information.sequencing_id()| is only accepted if no higher ids
  // were confirmed before; otherwise it is accepted unconditionally.
  void Confirm(SequenceInformation sequence_information,
               bool force,
               base::OnceCallback<void(Status)> completion_cb);

  // Initiates upload of collected records according to the priority.
  // Called usually for a queue with an infinite or very large upload period.
  // Multiple |Flush| calls can safely run in parallel.
  // Invokes |completion_cb| with error if upload fails or cannot start.
  void Flush(Priority priority, base::OnceCallback<void(Status)> completion_cb);

  // If the server attached signed encryption key to the response, it needs to
  // be paased here.
  void UpdateEncryptionKey(SignedEncryptionInfo signed_encryption_key);

  // Registers completion notification callback. Thread-safe.
  // All registered callbacks are called when all queues destructions come
  // to their completion and the Storage is destructed as well.
  void RegisterCompletionCallback(base::OnceClosure callback);

 private:
  friend class base::RefCountedThreadSafe<Storage>;

  // Private helper class to initialize a single queue
  friend class CreateQueueContext;

  // Private helper class to flush all queues with a given priority
  friend class FlushContext;

  // Map that associates <DM token, Priority> of users or the device with a
  // unique GenerationGuid which is then associated to a queue in the `queues_`
  // map. Only queues with their GenerationGuid in this map can be written to
  // and are considered "active". Queues that are not accepting new events (i.e.
  // queues that contained data before storage was shut down), will not have
  // their GenerationGuid in this map, but will still exists in the `queues_`
  // map so that they can send their remaining events.
  struct Hash {
    size_t operator()(const std::tuple<DMtoken, Priority>& v) const noexcept {
      static constexpr std::hash<DMtoken> dm_token_hasher;
      static constexpr std::hash<Priority> priority_hasher;
      const auto& [token, priority] = v;
      return dm_token_hasher(token) ^ priority_hasher(priority);
    }
  };
  using GenerationGuidMap =
      std::unordered_map<std::tuple<DMtoken, Priority>, GenerationGuid, Hash>;

  // Private constructor, to be called by Create factory method only.
  // Queues need to be added afterwards.
  explicit Storage(const Settings& settings);

  // Private destructor, as required by RefCountedThreadSafe.
  ~Storage();

  // Initializes the object by adding all queues for all priorities.
  // Must be called once and only once after construction.
  // Returns OK or error status, if anything failed to initialize.
  Status Init();

  // Helper method to select queue by priority on the Storage task runner and
  // then perform `queue_action`, if succeeded. Returns failure on any stage
  // with `completion_cb`.
  void AsyncGetQueueAndProceed(
      Priority priority,
      base::OnceCallback<void(scoped_refptr<StorageQueue>,
                              base::OnceCallback<void(Status)>)> queue_action,
      base::OnceCallback<void(Status)> completion_cb,
      StatusOr<GenerationGuid> generation_guid);

  // Creates a generation guid for this dm token, maps it to the dm token,
  // and returns the generation guid. Returns error if a generation guid exists
  // for this dm token already.
  StatusOr<GenerationGuid> CreateGenerationGuidForDMToken(
      const DMtoken& dm_token, Priority priority);

  // Returns the generation guid associated with `dm_token` or error if no
  // generation guid exists for `dm_token`.
  StatusOr<GenerationGuid> GetGenerationGuid(const DMtoken& dm_token,
                                             Priority priority);

  StatusOr<GenerationGuid> GetOrCreateGenerationGuid(const DMtoken& dm_token,
                                                     Priority priority);

  // Writes a record to the given queue.
  void WriteToQueue(Record record,
                    HealthModule::Recorder recorder,
                    scoped_refptr<StorageQueue> queue,
                    base::OnceCallback<void(Status)> completion_cb);

  // Immutable options, stored at the time of creation.
  const StorageOptions options_;

  // Task runner for storage-wide operations (initialized in
  // `queues_container_`).
  const scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  // Health module for debugging support. Exists always, but active only when
  // the `is_debugging_` flag is set.
  const scoped_refptr<HealthModule> health_module_;

  // Encryption module.
  const scoped_refptr<EncryptionModuleInterface> encryption_module_;

  // Internal module for initiail key delivery from server.
  const std::unique_ptr<KeyDelivery, base::OnTaskRunnerDeleter> key_delivery_;

  // Compression module.
  const scoped_refptr<CompressionModule> compression_module_;

  // Internal key management module.
  const std::unique_ptr<KeyInStorage> key_in_storage_;

  // Upload provider callback.
  const UploaderInterface::AsyncStartUploaderCb async_start_upload_cb_;

  // <DM token, Priority> -> Generation guid map
  GenerationGuidMap dmtoken_to_generation_guid_map_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Queues container and storage degradation controller. If degradation is
  // enabled, in case of disk space pressure it facilitates dropping low
  // priority events to free up space for the higher priority ones.
  const scoped_refptr<QueuesContainer> queues_container_;
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_NEW_STORAGE_H_
