// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_SSFC_UTILS_H_
#define RMAD_UTILS_SSFC_UTILS_H_

#include <string>

namespace rmad {

class SsfcUtils {
 public:
  SsfcUtils() = default;
  virtual ~SsfcUtils() = default;

  // Get the correct SSFC value via a specific script based on the model name.
  // false: Failure to calculate SSFC.
  // true: SSFC was calculated and assigned to |ssfc| or SSFC is not required.
  //       It depends on the value of |need_to_update|, if true, the ssfc is
  //       calculated and should be updated.
  virtual bool GetSSFC(const std::string& model,
                       bool* need_to_update,
                       uint32_t* ssfc) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_SSFC_UTILS_H_
