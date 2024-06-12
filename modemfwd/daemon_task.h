// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_DAEMON_TASK_H_
#define MODEMFWD_DAEMON_TASK_H_

#include <string>
#include <utility>

#include <base/time/time.h>
#include <brillo/any.h>
#include <brillo/variant_dictionary.h>

#include "modemfwd/daemon_delegate.h"

namespace modemfwd {

// The task class encapsulates a logical thread of work spawned by the daemon.
class Task {
 public:
  Task(Delegate* delegate, std::string name, std::string type);
  virtual ~Task();

  const std::string& name() { return name_; }
  const std::string& type() { return type_; }
  const base::Time& started_at() { return started_at_; }

  const brillo::VariantDictionary& props() { return props_; }

 protected:
  Delegate* delegate() { return delegate_; }

  void Finish();

  void SetProp(const std::string& key, brillo::Any value);
  void DeleteProp(const std::string& key);

  virtual void CancelOutstandingWork() {}

 private:
  Delegate* delegate_;
  std::string name_;
  std::string type_;
  base::Time started_at_;

  brillo::VariantDictionary props_;

  bool finished_explicitly_ = false;
};

}  // namespace modemfwd

#endif  // MODEMFWD_DAEMON_TASK_H_
