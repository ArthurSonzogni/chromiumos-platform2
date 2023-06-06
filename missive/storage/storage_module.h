// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_STORAGE_MODULE_H_
#define MISSIVE_STORAGE_STORAGE_MODULE_H_

#include <memory>
#include <queue>

#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>

#include "base/functional/callback_forward.h"
#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/encryption/verification.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/dynamic_flag.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

class StorageModule : public StorageModuleInterface, public DynamicFlag {
 public:
  // Factory method creates |StorageModule| object.
  static void Create(
      const StorageOptions& options,
      bool legacy_storage_enabled,
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

  bool legacy_storage_enabled() const;

 protected:
  // Constructor can only be called by |Create| factory method.
  explicit StorageModule(bool legacy_storage_enabled);

  // Refcounted object must have destructor declared protected or private.
  ~StorageModule() override;

 private:
  friend base::RefCountedThreadSafe<StorageModule>;

  // Called when DynamicFlag flips.
  void OnValueUpdate(bool is_enabled) const override;

  scoped_refptr<StorageInterface> storage_;
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_STORAGE_MODULE_H_
