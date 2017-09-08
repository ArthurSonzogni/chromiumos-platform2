// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_U2F_TOOL_H_
#define DEBUGD_SRC_U2F_TOOL_H_

#include <string>

#include <base/macros.h>

namespace debugd {

// Tool to tweak u2fd daemon.
class U2fTool {
 public:
  U2fTool() = default;
  ~U2fTool() = default;

  // Set override/debugging flags for u2fd.
  std::string SetFlags(const std::string& flags);

 private:
  DISALLOW_COPY_AND_ASSIGN(U2fTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_U2F_TOOL_H_
