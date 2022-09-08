// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_PROBE_TOOL_H_
#define DEBUGD_SRC_PROBE_TOOL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/values.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/process/process.h>

namespace debugd {

class ProbeTool {
 public:
  ProbeTool() = default;
  ProbeTool(const ProbeTool&) = delete;
  ProbeTool& operator=(const ProbeTool&) = delete;

  ~ProbeTool() = default;

  // Executes the function defined for runtime_probe.
  bool EvaluateProbeFunction(brillo::ErrorPtr* error,
                             const std::string& probe_statement,
                             int log_level,
                             brillo::dbus_utils::FileDescriptor* outfd,
                             brillo::dbus_utils::FileDescriptor* errfd);

  std::unique_ptr<brillo::Process> CreateSandboxedProcess(
      brillo::ErrorPtr* error, const std::string& probe_statement);

  bool GetValidMinijailArguments(brillo::ErrorPtr* error,
                                 const std::string& function_name,
                                 std::vector<std::string>* args_out);

 protected:
  void SetMinijailArgumentsForTesting(std::unique_ptr<base::Value> dict);

 private:
  bool LoadMinijailArguments(brillo::ErrorPtr* error);
  base::Value::Dict minijail_args_dict_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_PROBE_TOOL_H_
