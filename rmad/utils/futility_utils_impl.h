// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FUTILITY_UTILS_IMPL_H_
#define RMAD_UTILS_FUTILITY_UTILS_IMPL_H_

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>

#include "rmad/utils/cmd_utils.h"
#include "rmad/utils/futility_utils.h"
#include "rmad/utils/hwid_utils.h"

namespace rmad {

class FutilityUtilsImpl : public FutilityUtils {
 public:
  FutilityUtilsImpl();

  // Used to inject mock |cmd_utils_|, |hwid_utils_|, and |mtd_path_| for
  // testing.
  explicit FutilityUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils,
                             std::unique_ptr<HwidUtils> hwid_utils,
                             base::FilePath mtd_path);
  ~FutilityUtilsImpl() override = default;

  std::optional<bool> GetApWriteProtectionStatus() override;
  bool EnableApSoftwareWriteProtection() override;
  bool DisableApSoftwareWriteProtection() override;
  bool SetHwid(const std::string& hwid) override;
  std::optional<uint64_t> GetFlashSize() override;
  std::optional<FlashInfo> GetFlashInfo() override;

 private:
  std::optional<std::string> ParseFlashName(
      const std::string& flash_info_string);
  std::optional<std::pair<uint64_t, uint64_t>> ParseFlashWpsrRange(
      const std::string& flash_info_string);

  std::unique_ptr<CmdUtils> cmd_utils_;
  std::unique_ptr<HwidUtils> hwid_utils_;
  base::FilePath mtd_path_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FUTILITY_UTILS_IMPL_H_
