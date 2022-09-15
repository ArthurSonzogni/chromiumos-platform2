// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_LP_TOOLS_H_
#define DEBUGD_SRC_LP_TOOLS_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "debugd/src/process_with_output.h"

namespace debugd {

class LpTools {
 public:
  virtual ~LpTools() = default;

  // Runs lpadmin with the provided |arg_list| and |std_input|.
  virtual int Lpadmin(const ProcessWithOutput::ArgList& arg_list,
                      bool inherit_usergroups = false,
                      const std::vector<uint8_t>* std_input = nullptr) = 0;

  // Runs lpstat with the provided |arg_list| and |std_input|.
  virtual int Lpstat(const ProcessWithOutput::ArgList& arg_list,
                     std::string* output) = 0;

  virtual const base::FilePath& GetCupsPpdDir() const = 0;

  // Returns the exit code for the executed process.
  static int RunAsUser(const std::string& user,
                       const std::string& group,
                       const std::string& command,
                       const std::string& seccomp_policy,
                       const ProcessWithOutput::ArgList& arg_list,
                       const std::vector<uint8_t>* std_input = nullptr,
                       bool inherit_usergroups = false,
                       std::string* out = nullptr);
};

class LpToolsImpl : public LpTools {
 public:
  ~LpToolsImpl() override = default;

  int Lpadmin(const ProcessWithOutput::ArgList& arg_list,
              bool inherit_usergroups = false,
              const std::vector<uint8_t>* std_input = nullptr) override;

  int Lpstat(const ProcessWithOutput::ArgList& arg_list,
             std::string* output) override;

  const base::FilePath& GetCupsPpdDir() const override;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_LP_TOOLS_H_
