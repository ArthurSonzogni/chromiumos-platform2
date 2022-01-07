// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_CMD_UTILS_H_
#define RMAD_UTILS_FAKE_CMD_UTILS_H_

#include "rmad/utils/cmd_utils.h"

#include <string>
#include <vector>

namespace rmad {
namespace fake {

class FakeCmdUtils : public CmdUtils {
 public:
  FakeCmdUtils() = default;
  ~FakeCmdUtils() override = default;

  bool GetOutput(const std::vector<std::string>& argv,
                 std::string* output) const override {
    *output = "fake_output";
    return true;
  }

  bool GetOutputAndError(const std::vector<std::string>& argv,
                         std::string* output) const override {
    *output = "fake_output_with_error";
    return true;
  }
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_CMD_UTILS_H_
