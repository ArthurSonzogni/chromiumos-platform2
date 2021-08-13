// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_SESSION_H_
#define FEDERATED_FEDERATED_SESSION_H_

#include <string>

#include <fcp/fcp.h>

#include "federated/device_status_monitor.h"
#include "federated/example_database.h"
#include "federated/federated_metadata.h"

namespace federated {

// FederatedSession encapsulates essential elements for a client to run
// federated tasks, e.g. the function ptr from the library(`run_plan_`,
// `free_run_plan_result_`), the server config, the client_config.
class FederatedSession {
 public:
  // FederatedLibrary::CreateSession should be used instead of this constructor.
  FederatedSession(FlRunPlanFn run_plan,
                   FlFreeRunPlanResultFn free_run_plan_result,
                   const std::string& service_uri,
                   const std::string& api_key,
                   ClientConfigMetadata client_config,
                   const DeviceStatusMonitor* const device_status_monitor);
  FederatedSession& operator=(const FederatedSession&) = delete;
  ~FederatedSession();

  // Tries to checkin and start a federated task with the server, then updates
  // the client config, such as retry_token and next_retry_delay. It is
  // scheduled recurrently by Scheduler, see scheduler.cc for more details.
  void RunPlan(ExampleDatabase::Iterator&& example_iterator);
  // Resets `next_retry_delay_` to default. Called when current
  // `next_retry_delay_` elapses and a federated task is about to run.
  void ResetRetryDelay();
  std::string GetSessionName() const;

  base::TimeDelta next_retry_delay() const { return next_retry_delay_; }

 private:
  // Context provides several static functions used in constructing
  // FlTaskEnvironment that serves as hook for the library to e.g. request
  // examples.
  class Context {
   public:
    Context(const DeviceStatusMonitor* const device_status_monitor,
            ExampleDatabase::Iterator&& example_iterator);
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context();

    // Called by the library to get next example. `context` is effectively a
    // pointer to an Context instance, the same to the following methods.
    // Returns true if no errors, caller can construct a serialized example with
    // `data` and `size` if `end` is false, or it knows examples run out.
    // Returns false if any errors.
    static bool GetNextExample(const char** data,
                               int* const size,
                               bool* const end,
                               void* const context);
    // Called by the library to free the char* returned by GetNextExample.
    static void FreeExample(const char* const data, void* const context);
    // Called by the library to inquiry whether the current task should continue
    // or quit early.
    static bool TrainingConditionsSatisfied(void* const context);
    // Called by the library to publish event logs out to the daemon.
    static void PublishEvent(const char* const event,
                             const int size,
                             void* const context);

   private:
    // Not owned:
    const DeviceStatusMonitor* const device_status_monitor_;

    ExampleDatabase::Iterator example_iterator_;
  };

  const FlRunPlanFn run_plan_;
  const FlFreeRunPlanResultFn free_run_plan_result_;

  const std::string service_uri_;
  const std::string api_key_;

  ClientConfigMetadata client_config_;
  base::TimeDelta next_retry_delay_;

  // Not owned:
  const DeviceStatusMonitor* const device_status_monitor_;
};

}  // namespace federated

#endif  // FEDERATED_FEDERATED_SESSION_H_
