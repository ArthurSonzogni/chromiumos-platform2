// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FLASHROM_UTILS_IMPL_H_
#define RMAD_UTILS_FLASHROM_UTILS_IMPL_H_

#include <rmad/utils/flashrom_utils.h>

#include <memory>

#include "rmad/utils/cmd_utils.h"

namespace rmad {

class FlashromUtilsImpl : public FlashromUtils {
 public:
  FlashromUtilsImpl();
  explicit FlashromUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils);
  ~FlashromUtilsImpl() override = default;

  bool EnableSoftwareWriteProtection() override;
  bool DisableSoftwareWriteProtection() override;

 private:
  bool GetWriteProtectionRange(int* wp_start, int* wp_len);

  std::unique_ptr<CmdUtils> cmd_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FLASHROM_UTILS_IMPL_H_
