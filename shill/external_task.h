// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_EXTERNAL_TASK_H_
#define SHILL_EXTERNAL_TASK_H_

#include <sys/types.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/glib.h"
#include "shill/rpc_task.h"

namespace shill {

class ControlInterface;
class Error;
class EventDispatcher;
class ProcessKiller;

class ExternalTask : public RPCTaskDelegate {
 public:
  ExternalTask(ControlInterface* control,
               GLib* glib,
               const base::WeakPtr<RPCTaskDelegate>& task_delegate,
               const base::Callback<void(pid_t, int)>& death_callback);
  ~ExternalTask() override;  // But consider DestroyLater...

  // Schedule later deletion of the ExternalTask. Useful when in the
  // middle of an ExternalTask callback. Note that the caller _must_
  // release ownership of |this|. For example:
  //
  //   class Foo : public SupportsWeakPtr<Foo>, public RPCTaskDelegate {
  //    public:
  //      Foo() {
  //        task_.reset(new ExternalTask(...));
  //      }
  //
  //      void Notify(...) {
  //        task_.release()->DestroyLater(...);  // Passes ownership.
  //      }
  //
  //    private:
  //      std::unique_ptr<ExternalTask> task_;
  //   }
  void DestroyLater(EventDispatcher* dispatcher);

  // Forks off a process to run |program|, with the command-line
  // arguments |arguments|, and the environment variables specified in
  // |environment|.
  //
  // If |terminate_with_parent| is true, the child process will be
  // configured to terminate itself if this process dies. Otherwise,
  // the child process will retain its default behavior.
  //
  // On success, returns true, and leaves |error| unmodified.
  // On failure, returns false, and sets |error|.
  //
  // |environment| SHOULD NOT contain kRPCTaskServiceVariable or
  // kRPCTaskPathVariable, as that may prevent the child process
  // from communicating back to the ExternalTask.
  virtual bool Start(const base::FilePath& program,
                     const std::vector<std::string>& arguments,
                     const std::map<std::string, std::string>& environment,
                     bool terminate_with_parent,
                     Error* error);
  virtual void Stop();

 private:
  friend class ExternalTaskTest;
  FRIEND_TEST(ExternalTaskTest, Destructor);
  FRIEND_TEST(ExternalTaskTest, GetLogin);
  FRIEND_TEST(ExternalTaskTest, Notify);
  FRIEND_TEST(ExternalTaskTest, OnTaskDied);
  FRIEND_TEST(ExternalTaskTest, Start);
  FRIEND_TEST(ExternalTaskTest, Stop);
  FRIEND_TEST(ExternalTaskTest, StopNotStarted);

  // Implements RPCTaskDelegate.
  void GetLogin(std::string* user, std::string* password) override;
  void Notify(
      const std::string& event,
      const std::map<std::string, std::string>& details) override;
  // Called when the external process exits.
  static void OnTaskDied(GPid pid, gint status, gpointer data);

  static void Destroy(ExternalTask* task);

  // This method is run in the child process (i.e. after fork(), but
  // before exec()). It configures the child to receive a SIGTERM when
  // the parent exits.
  static void SetupTermination(gpointer glib_user_data);

  ControlInterface* control_;
  GLib* glib_;
  ProcessKiller* process_killer_;  // Field permits mocking.

  std::unique_ptr<RPCTask> rpc_task_;
  base::WeakPtr<RPCTaskDelegate> task_delegate_;
  base::Callback<void(pid_t, int)> death_callback_;

  // The PID of the spawned process. May be 0 if no process has been
  // spawned yet or the process has died.
  pid_t pid_;

  // Child exit watch callback source tag.
  unsigned int child_watch_tag_;

  DISALLOW_COPY_AND_ASSIGN(ExternalTask);
};

}  // namespace shill

#endif  // SHILL_EXTERNAL_TASK_H_
