// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_DAEMON_TASK_H_
#define MODEMFWD_DAEMON_TASK_H_

#include <string>
#include <utility>

#include <base/time/time.h>

#include "modemfwd/daemon_delegate.h"
#include "modemfwd/logging.h"

namespace modemfwd {

// The task class encapsulates a logical thread of work spawned by the daemon.
class Task {
 public:
  Task(Delegate* delegate, std::string name, std::string type)
      : delegate_(delegate),
        name_(std::move(name)),
        type_(std::move(type)),
        started_at_(base::Time::Now()) {
    ELOG(INFO) << "Task " << name_ << " was created";
  }
  virtual ~Task() {
    if (!finished_explicitly_) {
      ELOG(INFO) << "Task " << name_ << " was destroyed";
    }
  }

  const std::string& name() { return name_; }
  const std::string& type() { return type_; }
  const base::Time& started_at() { return started_at_; }

 protected:
  Delegate* delegate() { return delegate_; }

  void Finish() {
    ELOG(INFO) << "Task " << name_ << " finished";
    finished_explicitly_ = true;
    CancelOutstandingWork();
    delegate_->FinishTask(this);
  }

  virtual void CancelOutstandingWork() {}

 private:
  Delegate* delegate_;
  std::string name_;
  std::string type_;
  base::Time started_at_;
  bool finished_explicitly_ = false;
};

}  // namespace modemfwd

#endif  // MODEMFWD_DAEMON_TASK_H_
