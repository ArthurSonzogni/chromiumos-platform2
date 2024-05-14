// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_KEY_DELIVERY_H_
#define MISSIVE_STORAGE_KEY_DELIVERY_H_

#include <memory>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/thread_annotations.h>
#include <base/timer/timer.h>

#include "missive/encryption/encryption_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"

namespace reporting {

// Class for key upload/download to the file system in storage.
class KeyDelivery {
 public:
  using RequestCallback = base::OnceCallback<void(Status)>;

  // Key delivery UMA name
  static constexpr char kResultUma[] = "Platform.Missive.KeyDeliveryResult";

  // Factory method, returns smart pointer with deletion on sequence.
  static std::unique_ptr<KeyDelivery, base::OnTaskRunnerDeleter> Create(
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb);

  ~KeyDelivery();

  // Makes a request to update key, invoking `callback` once responded.
  // If `is_mandatory` is false and the request is not the first, do
  // nothing and just drop `callback`.
  void Request(bool is_mandatory, RequestCallback callback);

  void OnCompletion(Status status);

  // Starts periodic updates of the key (every time `period` has passed).
  // Does nothing if the periodic update is already scheduled.
  // Should be called after the initial key is set up.
  void StartPeriodicKeyUpdate(const base::TimeDelta period);

 private:
  // Constructor called by factory only.
  explicit KeyDelivery(
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
      scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner);

  void RequestKeyIfNeeded();

  void EnqueueRequestAndPossiblyStart(
      bool is_mandatory,  // just like in `Request`
      RequestCallback callback);

  void PostResponses(Status status);

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

  // Used to periodically trigger check for encryption key
  base::RepeatingTimer upload_timer_ GUARDED_BY_CONTEXT(sequence_checker_);
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_KEY_DELIVERY_H_
