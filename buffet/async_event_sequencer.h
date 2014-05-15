// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_ASYNC_EVENT_SEQUENCER_H_
#define BUFFET_ASYNC_EVENT_SEQUENCER_H_

#include <set>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/basictypes.h>
#include <base/memory/ref_counted.h>

namespace buffet {

namespace dbus_utils {

// A helper class for coordinating the multiple async tasks.  A consumer
// may grab any number of callbacks via Get*Handler() and schedule a list
// of completion actions to take.  When all handlers obtained via Get*Handler()
// have been called, the AsyncEventSequencer will call its CompletionActions.
//
// Usage:
//
// void Init(const base::Callback<void(bool success)> cb) {
//   scoped_refptr<AsyncEventSequencer> sequencer(
//       new AsyncEventSequencer());
//   one_delegate_needing_init_.Init(sequencer->GetHandler(
//       "my delegate failed to init", false));
//   dbus_init_delegate_.Init(sequencer->GetExportHandler(
//       "org.test.Interface", "ExposedMethodName",
//       "another delegate is flaky", false));
//   sequencer->OnAllTasksCompletedCall({cb});
// }
class AsyncEventSequencer : public base::RefCounted<AsyncEventSequencer> {
 public:
  typedef base::Callback<void(bool success)> Handler;
  typedef base::Callback<void (const std::string& interface_name,
                               const std::string& method_name,
                               bool success)> ExportHandler;
  typedef base::Callback<void(bool all_succeeded)> CompletionAction;
  typedef base::Callback<void(void)> CompletionTask;

  AsyncEventSequencer();

  // Get a Finished handler callback.  Each callback is "unique" in the sense
  // that subsequent calls to GetHandler() will create new handlers
  // which will need to be called before completion actions are run.
  Handler GetHandler(const std::string& descriptive_message,
                     bool failure_is_fatal);

  // Like GetHandler except with a signature tailored to
  // ExportedObject's ExportMethod callback requirements.  Will also assert
  // that the passed interface/method names from ExportedObject are correct.
  ExportHandler GetExportHandler(
      const std::string& interface_name, const std::string& method_name,
      const std::string& descriptive_message, bool failure_is_fatal);

  // Once all handlers obtained via GetHandler have run,
  // we'll run each CompletionAction, then discard our references.
  // No more handlers may be obtained after this call.
  void OnAllTasksCompletedCall(std::vector<CompletionAction> actions);

  // Wrap a CompletionTask with a function that discards the result.
  // This CompletionTask retains no references to the AsyncEventSequencer.
  CompletionAction WrapCompletionTask(const CompletionTask& task);

 private:
  // We'll partially bind this function before giving it back via
  // GetHandler.  Note that the returned callbacks have
  // references to *this, which gives us the neat property that we'll
  // destroy *this only when all our callbacks have been destroyed.
  void HandleFinish(int registration_number, const std::string& error_message,
                    bool failure_is_fatal, bool success);
  // Similar to HandleFinish.
  void HandleDBusMethodExported(
      const Handler& finish_handler,
      const std::string& expected_interface_name,
      const std::string& expected_method_name,
      const std::string& actual_interface_name,
      const std::string& actual_method_name,
      bool success);
  void RetireRegistration(int registration_number);
  void CheckForFailure(bool failure_is_fatal, bool success,
                       const std::string& error_message);
  void PossiblyRunCompletionActions();

  bool started_{false};          // NOLINT - initializer list
  int registration_counter_{0};  // NOLINT - initializer list
  std::set<int> outstanding_registrations_;
  std::vector<CompletionAction> completion_actions_;
  bool had_failures_{false};     // NOLINT - initializer list
  // Ref counted objects have private destructors.
  ~AsyncEventSequencer();
  friend class base::RefCounted<AsyncEventSequencer>;
  DISALLOW_COPY_AND_ASSIGN(AsyncEventSequencer);
};

}  // namespace dbus_utils

}  // namespace buffet

#endif  // BUFFET_ASYNC_EVENT_SEQUENCER_H_
