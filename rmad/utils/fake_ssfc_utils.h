// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_SSFC_UTILS_H_
#define RMAD_UTILS_FAKE_SSFC_UTILS_H_

#include "rmad/utils/ssfc_utils.h"

#include <string>

namespace rmad {
namespace fake {

class FakeSsfcUtils : public SsfcUtils {
 public:
  FakeSsfcUtils() = default;
  ~FakeSsfcUtils() override = default;

  bool GetSSFC(const std::string& model,
               bool* need_to_update,
               uint32_t* ssfc) const override;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_SSFC_UTILS_H_
