// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/daemon_task.h"

#include <string>

#include <base/time/time.h>
#include <brillo/any.h>
#include <brillo/variant_dictionary.h>

#include "modemfwd/logging.h"

namespace modemfwd {

Task::Task(Delegate* delegate, std::string name, std::string type)
    : delegate_(delegate),
      name_(std::move(name)),
      type_(std::move(type)),
      started_at_(base::Time::Now()) {
  ELOG(INFO) << "Task " << name_ << " was created";
}

Task::~Task() {
  if (!finished_explicitly_) {
    ELOG(INFO) << "Task " << name_ << " was destroyed";
  }
}

void Task::Finish() {
  ELOG(INFO) << "Task " << name_ << " finished";
  finished_explicitly_ = true;
  CancelOutstandingWork();
  delegate_->FinishTask(this);
}

void Task::SetProp(const std::string& key, brillo::Any value) {
  if (value.IsEmpty()) {
    DeleteProp(key);
    return;
  }
  if (props_[key] == value) {
    return;
  }

  props_[key] = std::move(value);
  delegate_->TaskUpdated(this);
}

void Task::DeleteProp(const std::string& key) {
  if (props_.count(key) == 0) {
    return;
  }

  props_.erase(key);
  delegate_->TaskUpdated(this);
}

}  // namespace modemfwd
