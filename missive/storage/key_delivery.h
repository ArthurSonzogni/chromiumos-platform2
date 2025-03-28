// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_KEY_DELIVERY_H_
#define MISSIVE_STORAGE_KEY_DELIVERY_H_

#include <memory>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/memory/weak_ptr.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/thread_annotations.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "missive/encryption/encryption_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"

namespace reporting {

// Class for key upload/download to the file system in storage.
class KeyDelivery {
 public:
  using RequestCallback = base::OnceCallback<void(Status)>;

  // Key delivery UMA name.
  static constexpr char kResultUma[] = "Platform.Missive.KeyDeliveryResult";

  // Factory method, returns smart pointer with deletion on sequence.
  static std::unique_ptr<KeyDelivery, base::OnTaskRunnerDeleter> Create(
      base::TimeDelta key_check_period,
      base::TimeDelta lazy_key_check_period,
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb);

  ~KeyDelivery();

  // Makes a request to update key, invoking `callback` once responded (if
  // specified).
  void Request(RequestCallback callback);

  // Starts periodic updates of the key (every time `period` has passed).
  // Does nothing if the periodic update is already scheduled.
  // Should be called after the initial key is set up.
  void ScheduleNextKeyUpdate();

  // Called upon key update success/failure.
  void OnKeyUpdateResult(Status status);

 private:
  // Constructor called by factory only.
  explicit KeyDelivery(
      base::TimeDelta key_check_period,
      base::TimeDelta lazy_key_check_period,
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
      scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner);

  static void RequestKeyIfNeeded(base::WeakPtr<KeyDelivery> self);

  void EnqueueRequestAndPossiblyStart(RequestCallback callback);

  void PostResponses(Status status);

  void EncryptionKeyReceiverReady(
      StatusOr<std::unique_ptr<UploaderInterface>> uploader_result);

  const scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  // Period of checking possible key update.
  const base::TimeDelta key_check_period_;  // eager - when there is no key.
  const base::TimeDelta
      lazy_key_check_period_;  // lazy - when the key is present, but may be
                               // outdated.

  // Upload provider callback.
  const UploaderInterface::AsyncStartUploaderCb async_start_upload_cb_;

  // List of all request callbacks.
  std::vector<RequestCallback> callbacks_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Used to check whether or not encryption is enabled and if we need to
  // request the key.
  const scoped_refptr<EncryptionModuleInterface> encryption_module_;

  // Used to schedule the next check for encryption key.
  base::RetainingOneShotTimer request_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Weak pointer factory.
  base::WeakPtrFactory<KeyDelivery> weak_ptr_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_KEY_DELIVERY_H_
