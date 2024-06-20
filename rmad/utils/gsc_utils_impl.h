// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_GSC_UTILS_IMPL_H_
#define RMAD_UTILS_GSC_UTILS_IMPL_H_

#include <rmad/utils/gsc_utils.h>

#include <memory>
#include <string>

#include "rmad/utils/cmd_utils.h"

namespace rmad {

class GscUtilsImpl : public GscUtils {
 public:
  GscUtilsImpl();
  explicit GscUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils);
  ~GscUtilsImpl() override = default;

  bool GetRsuChallengeCode(std::string* challenge_code) const override;
  bool PerformRsu(const std::string& unlock_code) const override;
  bool EnableFactoryMode() const override;
  bool DisableFactoryMode() const override;
  bool IsFactoryModeEnabled() const override;
  bool IsInitialFactoryModeEnabled() const override;
  std::optional<std::string> GetBoardIdType() const override;
  std::optional<std::string> GetBoardIdFlags() const override;
  bool SetBoardId(bool is_custom_label) const override;
  bool Reboot() const override;
  std::optional<FactoryConfig> GetFactoryConfig() const override;
  bool SetFactoryConfig(bool is_chassis_branded,
                        int hw_compliance_version) const override;
  std::optional<bool> GetChassisOpenStatus() override;
  SpiAddressingMode GetAddressingMode() override;
  bool SetAddressingMode(SpiAddressingMode mode) override;
  SpiAddressingMode GetAddressingModeByFlashSize(uint64_t flash_size) override;
  bool SetWpsr(std::string_view wpsr) override;
  std::optional<bool> IsApWpsrProvisioned() override;

 private:
  std::unique_ptr<CmdUtils> cmd_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_GSC_UTILS_IMPL_H_
