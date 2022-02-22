// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_

#include <string>

#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>
#include <base/files/file_path.h>

#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"

namespace diagnostics {

class SystemConfig final : public SystemConfigInterface {
 public:
  SystemConfig(brillo::CrosConfigInterface* cros_config,
               DebugdAdapter* debugd_adapter);
  // Constructor that overrides root_dir is only meant to be used for testing.
  SystemConfig(brillo::CrosConfigInterface* cros_config,
               DebugdAdapter* debugd_adapter,
               const base::FilePath& root_dir);
  SystemConfig(const SystemConfig&) = delete;
  SystemConfig& operator=(const SystemConfig&) = delete;
  ~SystemConfig() override;

  // SystemConfigInterface overrides:
  bool FioSupported() override;
  bool HasBacklight() override;
  bool HasBattery() override;
  bool HasSmartBattery() override;
  bool HasSkuNumber() override;
  bool NvmeSupported() override;
  bool NvmeSelfTestSupported() override;
  bool SmartCtlSupported() override;
  bool IsWilcoDevice() override;
  base::Optional<std::string> GetMarketingName() override;
  base::Optional<std::string> GetOemName() override;
  std::string GetCodeName() override;

 private:
  // Unowned pointer. The CrosConfigInterface should outlive this instance.
  brillo::CrosConfigInterface* cros_config_;
  // Unowned pointer. The DebugdAdapter should outlive this instance.
  DebugdAdapter* debugd_adapter_;
  base::FilePath root_dir_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_
