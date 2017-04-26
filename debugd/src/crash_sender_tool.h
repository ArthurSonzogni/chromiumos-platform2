// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_CRASH_SENDER_TOOL_H_
#define DEBUGD_SRC_CRASH_SENDER_TOOL_H_

#include <base/macros.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class CrashSenderTool : public SubprocessTool {
 public:
  CrashSenderTool() = default;
  ~CrashSenderTool() override = default;

  void UploadCrashes();

 private:
  DISALLOW_COPY_AND_ASSIGN(CrashSenderTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CRASH_SENDER_TOOL_H_
