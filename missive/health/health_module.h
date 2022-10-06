// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_HEALTH_HEALTH_MODULE_H_
#define MISSIVE_HEALTH_HEALTH_MODULE_H_

#include <memory>

#include <base/bind.h>
#include <base/sequence_checker.h>
#include <base/memory/ref_counted.h>

#include "missive/health/health_module_delegate.h"
#include "missive/proto/record.pb.h"
#include "missive/util/status.h"

namespace reporting {

// The HealthModule class is used by other modules in the ERP to update and
// gather health related info. This class delegates the implementation logic to
// the HealthModuleDelegate and ensures that all calls to read and write data
// are done with mutual exclusion
class HealthModule : public base::RefCountedThreadSafe<HealthModule> {
 public:
  // Static class factory method.
  static scoped_refptr<HealthModule> Create(
      std::unique_ptr<HealthModuleDelegate> delegate);

  HealthModule(const HealthModule& other) = delete;
  HealthModule& operator=(const HealthModule& other) = delete;

  // Adds history record to local memory. Triggers a write to health files.
  void PostHealthRecord(HealthDataHistory history);

  // Gets health data and send to |cb|.
  void GetHealthData(base::OnceCallback<void(const ERPHealthData)> cb);

 protected:
  // Constructor can only be called by |Create| factory method.
  HealthModule(std::unique_ptr<HealthModuleDelegate> delegate,
               scoped_refptr<base::SequencedTaskRunner> task_runner);

  // HealthModuleDelegate controlling read/write logic.
  std::unique_ptr<HealthModuleDelegate> delegate_;

  virtual ~HealthModule();  // `virtual` is mandated by RefCounted.

 private:
  // Task Runner which tasks are posted to.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  friend base::RefCountedThreadSafe<HealthModule>;
};
}  // namespace reporting

#endif  // MISSIVE_HEALTH_HEALTH_MODULE_H_
