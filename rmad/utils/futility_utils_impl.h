// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FUTILITY_UTILS_IMPL_H_
#define RMAD_UTILS_FUTILITY_UTILS_IMPL_H_

#include <rmad/utils/futility_utils.h>

#include <memory>
#include <string>

#include "rmad/utils/cmd_utils.h"
#include "rmad/utils/hwid_utils.h"

namespace rmad {

class FutilityUtilsImpl : public FutilityUtils {
 public:
  FutilityUtilsImpl();

  // Used to inject mock |cmd_utils_| and |hwid_utils_| for testing.
  explicit FutilityUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils,
                             std::unique_ptr<HwidUtils> hwid_utils);
  ~FutilityUtilsImpl() override = default;

  bool GetApWriteProtectionStatus(bool* enabled) override;
  bool EnableApSoftwareWriteProtection() override;
  bool DisableApSoftwareWriteProtection() override;
  bool SetHwid(const std::string& hwid) override;

 private:
  std::unique_ptr<CmdUtils> cmd_utils_;
  std::unique_ptr<HwidUtils> hwid_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FUTILITY_UTILS_IMPL_H_
