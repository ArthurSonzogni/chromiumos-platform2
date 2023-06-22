// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_STORAGE_MODULE_H_
#define MISSIVE_STORAGE_STORAGE_MODULE_H_

#include <memory>
#include <queue>
#include <string>

#include <base/functional/callback.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_piece_forward.h>
#include <base/task/sequenced_task_runner.h>

#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/encryption/verification.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_base.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/statusor.h"

namespace reporting {

class StorageModule : public StorageModuleInterface {
 public:
  // Factory method creates |StorageModule| object.
  static void Create(
      const StorageOptions& options,
      base::StringPiece legacy_storage_enabled,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
      scoped_refptr<QueuesContainer> queues_container,
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      scoped_refptr<CompressionModule> compression_module,
      scoped_refptr<SignatureVerificationDevFlag>
          signature_verification_dev_flag,
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
          callback);

  StorageModule(const StorageModule& other) = delete;
  StorageModule& operator=(const StorageModule& other) = delete;

  // AddRecord will add |record| (taking ownership) to the |StorageModule|
  // according to the provided |priority|. On completion, |callback| will be
  // called.
  void AddRecord(Priority priority,
                 Record record,
                 EnqueueCallback callback) override;

  // Initiates upload of collected records according to the priority.
  // Called usually for a queue with an infinite or very large upload period.
  // Multiple |Flush| calls can safely run in parallel.
  // Returns error if cannot start upload.
  void Flush(Priority priority, FlushCallback callback) override;

  // Once a record has been successfully uploaded, the sequence information
  // can be passed back to the StorageModule here for record deletion.
  // If |force| is false (which is used in most cases), |sequence_information|
  // only affects Storage if no higher sequencing was confirmed before;
  // otherwise it is accepted unconditionally.
  // Declared virtual for testing purposes.
  virtual void ReportSuccess(SequenceInformation sequence_information,
                             bool force);

  // If the server attached signed encryption key to the response, it needs to
  // be paased here.
  // Declared virtual for testing purposes.
  virtual void UpdateEncryptionKey(SignedEncryptionInfo signed_encryption_key);

  // Parse list of priorities to be in legacy single-generation action state
  // from now on. All other priorities are in multi-generation action state.
  void SetLegacyEnabledPriorities(base::StringPiece legacy_storage_enabled);

 protected:
  // Constructor can only be called by |Create| factory method.
  explicit StorageModule(
      const StorageOptions& options,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
      scoped_refptr<QueuesContainer> queues_container,
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      scoped_refptr<CompressionModule> compression_module,
      scoped_refptr<SignatureVerificationDevFlag>
          signature_verification_dev_flag);

  // Refcounted object must have destructor declared protected or private.
  ~StorageModule() override;

  // Returns a callback that initializes `instance->storage_`.
  [[nodiscard("Call .Run() on return value.")]] static base::OnceClosure
  InitStorageAsync(
      scoped_refptr<StorageModule> instance,
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
          callback);

  // Sets `storage_` to a valid `StorageInterface` or returns error status via
  // `callback`.
  void SetStorage(
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)> callback,
      StatusOr<scoped_refptr<StorageInterface>> storage);

  void InjectStorageUnavailableErrorForTesting();

 private:
  friend class StorageModuleTest;
  friend base::RefCountedThreadSafe<StorageModule>;

  // Task runner for serializing storage operations and setting internal
  // state.
  const scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  // Reference to `Storage` object.
  // Note: all accesses to `storage_` should be done on StorageModule's
  // sequenced task runner since via StorageModule::AsyncSetStorage may change
  // the object `storage_` points to.
  scoped_refptr<StorageInterface> storage_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Parameters used to create Storage
  const StorageOptions options_;
  const UploaderInterface::AsyncStartUploaderCb async_start_upload_cb_;
  const scoped_refptr<QueuesContainer> queues_container_;
  const scoped_refptr<EncryptionModuleInterface> encryption_module_;
  const scoped_refptr<CompressionModule> compression_module_;
  const scoped_refptr<SignatureVerificationDevFlag>
      signature_verification_dev_flag_;

  // Callback for testing the result of `AsyncSetStorage` function.
  base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
      on_storage_set_cb_for_testing_;
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_STORAGE_MODULE_H_
