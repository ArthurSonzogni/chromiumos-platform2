// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_external_task.h"

namespace shill {

MockExternalTask::MockExternalTask(
    ControlInterface* control,
    GLib* glib,
    const base::WeakPtr<RPCTaskDelegate>& task_delegate,
    const base::Callback<void(pid_t, int)>& death_callback)
    : ExternalTask(control, glib, task_delegate, death_callback) {}

MockExternalTask::~MockExternalTask() {
  OnDelete();
}

}  // namespace shill
