// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_SCHEDULER_CONFIRM_RECORDS_JOB_H_
#define MISSIVE_SCHEDULER_CONFIRM_RECORDS_JOB_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_method_response.h>

#include "missive/health/health_module.h"
#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module.h"
#include "missive/util/status.h"

namespace reporting {

class ConfirmRecordsJob : public Scheduler::Job {
 public:
  class ConfirmRecordsResponseDelegate : public Job::JobDelegate {
   public:
    ConfirmRecordsResponseDelegate(
        scoped_refptr<HealthModule> health_module,
        std::unique_ptr<
            brillo::dbus_utils::DBusMethodResponse<ConfirmRecordUploadResponse>>
            response);

   private:
    Status Complete() override;
    Status Cancel(Status status) override;

    Status SendResponse(Status status);

    // Task runner for final operations to take place on.
    // Matches the thread constructor was called on.
    scoped_refptr<base::SequencedTaskRunner> task_runner_;

    scoped_refptr<HealthModule> health_module_;

    // response_ can only be used once - the logic in Scheduler::Job ensures
    // that only Complete or Cancel are every called once.
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<ConfirmRecordUploadResponse>>
        response_;
  };

  ConfirmRecordsJob(const ConfirmRecordsJob& other) = delete;
  ConfirmRecordsJob& operator=(const ConfirmRecordsJob& other) = delete;

  static SmartPtr<ConfirmRecordsJob> Create(
      scoped_refptr<StorageModule> storage_module,
      scoped_refptr<HealthModule> health_module,
      ConfirmRecordUploadRequest request,
      std::unique_ptr<ConfirmRecordsResponseDelegate> delegate);

 protected:
  // ConfirmRecordsJob::StartImpl expects ConfirmRecordUploadRequest to include
  // a valid file descriptor and the pid of the owner. Permissions of the file
  // descriptor must be set by the owner such that the Missive Daemon can open
  // it. Utilizing a file descriptor allows us to avoid a copy from DBus and
  // then another copy to Missive. The file descriptor **must** point to a
  // memory mapped file and not an actual file, as device and user data cannot
  // be copied to disk without encryption.
  void StartImpl() override;

 private:
  ConfirmRecordsJob(
      scoped_refptr<StorageModule> storage_module,
      scoped_refptr<HealthModule> health_module,
      scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
      ConfirmRecordUploadRequest request,
      std::unique_ptr<ConfirmRecordsResponseDelegate> delegate);

  const scoped_refptr<StorageModule> storage_module_;
  scoped_refptr<HealthModule> health_module_;
  const ConfirmRecordUploadRequest request_;
  base::WeakPtrFactory<ConfirmRecordsJob> weak_ptr_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_SCHEDULER_CONFIRM_RECORDS_JOB_H_
