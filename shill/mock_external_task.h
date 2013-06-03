// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_EXTERNAL_TASK_H_
#define SHILL_MOCK_EXTERNAL_TASK_H_

#include <gmock/gmock.h>

#include "shill/external_task.h"

namespace shill {

class MockExternalTask : public ExternalTask {
public:
  MockExternalTask(ControlInterface *control,
                   GLib *glib,
                   const base::WeakPtr<RPCTaskDelegate> &task_delegate,
                   const base::Callback<void(pid_t, int)> &death_callback);
  virtual ~MockExternalTask();

  MOCK_METHOD4(Start,
               bool(const base::FilePath &file,
                    const std::vector<std::string> &arguments,
                    const std::map<std::string, std::string> &environment,
                    Error *error));
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD0(OnDelete, void());

private:
  DISALLOW_COPY_AND_ASSIGN(MockExternalTask);
};

}  // namespace shill

#endif  // SHILL_MOCK_EXTERNAL_TASK_H_
