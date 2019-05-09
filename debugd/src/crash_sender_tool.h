// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_CRASH_SENDER_TOOL_H_
#define DEBUGD_SRC_CRASH_SENDER_TOOL_H_

#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <brillo/errors/error.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class CrashSenderTool : public SubprocessTool {
 public:
  static constexpr char kErrorBadFileName[] =
      "org.chromium.debugd.error.BadFileName";

  CrashSenderTool() = default;
  ~CrashSenderTool() override = default;

  // Run crash_sender to upload any crashes currently on the system.
  void UploadCrashes();

  // Run crash_sender to upload the crash given in the files in |in_files|.
  bool UploadSingleCrash(
      const std::vector<std::tuple<std::string, base::ScopedFD>>& in_files,
      brillo::ErrorPtr* error);

 private:
  int next_crash_directory_id_ = 1;

  void RunCrashSender(bool ignore_hold_off_time,
                      const base::FilePath& crash_directory);
  DISALLOW_COPY_AND_ASSIGN(CrashSenderTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CRASH_SENDER_TOOL_H_
