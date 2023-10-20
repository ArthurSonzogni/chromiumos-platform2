// Copyright 2023 The ChromiumOS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Tool to run crosh.

#ifndef DEBUGD_SRC_CROSH_TOOL_H_
#define DEBUGD_SRC_CROSH_TOOL_H_

#include <string>

#include <base/files/scoped_file.h>
#include <brillo/errors/error.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class CroshTool : public SubprocessTool {
 public:
  CroshTool() = default;
  CroshTool(const CroshTool&) = delete;
  CroshTool& operator=(const CroshTool&) = delete;

  ~CroshTool() override = default;

  bool Run(const base::ScopedFD& infd,
           const base::ScopedFD& outfd,
           std::string* out_id,
           brillo::ErrorPtr* error);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CROSH_TOOL_H_
