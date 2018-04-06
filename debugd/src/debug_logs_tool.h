// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_DEBUG_LOGS_TOOL_H_
#define DEBUGD_SRC_DEBUG_LOGS_TOOL_H_

#include <base/files/scoped_file.h>
#include <base/macros.h>

namespace debugd {

class DebugLogsTool {
 public:
  DebugLogsTool() = default;
  ~DebugLogsTool() = default;

  void GetDebugLogs(bool is_compressed,
                    const base::ScopedFD& fd);

 private:
  DISALLOW_COPY_AND_ASSIGN(DebugLogsTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_DEBUG_LOGS_TOOL_H_
