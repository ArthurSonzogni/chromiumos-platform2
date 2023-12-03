// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Tool to run crosh's shell cmd.

#ifndef DEBUGD_SRC_CROSH_SHELL_TOOL_H_
#define DEBUGD_SRC_CROSH_SHELL_TOOL_H_

#include <string>

#include <base/files/scoped_file.h>
#include <brillo/errors/error.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class CroshShellTool : public SubprocessTool {
 public:
  CroshShellTool() = default;
  CroshShellTool(const CroshShellTool&) = delete;
  CroshShellTool& operator=(const CroshShellTool&) = delete;

  ~CroshShellTool() override = default;

  bool Run(const base::ScopedFD& infd,
           const base::ScopedFD& outfd,
           std::string* out_id,
           brillo::ErrorPtr* error);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CROSH_SHELL_TOOL_H_
