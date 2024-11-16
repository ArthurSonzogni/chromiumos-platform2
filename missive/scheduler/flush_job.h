// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_SCHEDULER_FLUSH_JOB_H_
#define MISSIVE_SCHEDULER_FLUSH_JOB_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_method_response.h>

#include "missive/health/health_module.h"
#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/util/status.h"

namespace reporting {

class FlushJob : public Scheduler::Job {
 public:
  class FlushResponseDelegate : public Job::JobDelegate {
   public:
    FlushResponseDelegate(
        scoped_refptr<HealthModule> health_module,
        std::unique_ptr<
            brillo::dbus_utils::DBusMethodResponse<FlushPriorityResponse>>
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
        brillo::dbus_utils::DBusMethodResponse<FlushPriorityResponse>>
        response_;
  };

  FlushJob(const FlushJob& other) = delete;
  FlushJob& operator=(const FlushJob& other) = delete;

  static SmartPtr<FlushJob> Create(
      scoped_refptr<StorageModuleInterface> storage_module,
      scoped_refptr<HealthModule> health_module,
      FlushPriorityRequest request,
      std::unique_ptr<FlushResponseDelegate> delegate);

 protected:
  // FlushJob::StartImpl expects FlushPriorityRequest to include a valid file
  // descriptor and the pid of the owner. Permissions of the file descriptor
  // must be set by the owner such that the Missive Daemon can open it.
  // Utilizing a file descriptor allows us to avoid a copy from DBus and then
  // another copy to Missive.
  // The file descriptor **must** point to a memory mapped file and not an
  // actual file, as device and user data cannot be copied to disk without
  // encryption.
  void StartImpl() override;

 private:
  FlushJob(scoped_refptr<StorageModuleInterface> storage_module,
           scoped_refptr<HealthModule> health_module,
           scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
           FlushPriorityRequest request,
           std::unique_ptr<FlushResponseDelegate> delegate);

  scoped_refptr<StorageModuleInterface> storage_module_;
  scoped_refptr<HealthModule> health_module_;
  const FlushPriorityRequest request_;
  base::WeakPtrFactory<FlushJob> weak_ptr_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_SCHEDULER_FLUSH_JOB_H_
