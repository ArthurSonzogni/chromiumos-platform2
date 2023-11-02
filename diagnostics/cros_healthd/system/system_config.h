// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/system/system_config_interface.h"

namespace org::chromium {
class debugdProxyInterface;
}  // namespace org::chromium

namespace diagnostics {
class CrosConfig;

// Deprecated: Don't add new fields to this class. Use `GroundTruth` instead.
class SystemConfig final : public SystemConfigInterface {
 public:
  SystemConfig(CrosConfig* cros_config,
               org::chromium::debugdProxyInterface* debugd_proxy);
  SystemConfig(const SystemConfig&) = delete;
  SystemConfig& operator=(const SystemConfig&) = delete;
  ~SystemConfig() override;

  // SystemConfigInterface overrides:
  bool HasBacklight() override;
  bool HasBattery() override;
  bool HasSmartBattery() override;
  bool HasSkuNumber() override;
  bool HasPrivacyScreen() override;
  bool HasChromiumEC() override;
  bool NvmeSupported() override;
  void NvmeSelfTestSupported(NvmeSelfTestSupportedCallback callback) override;
  bool SmartCtlSupported() override;
  bool MmcSupported() override;
  bool FingerprintDiagnosticSupported() override;
  bool IsWilcoDevice() override;
  std::optional<std::string> GetMarketingName() override;
  std::optional<std::string> GetOemName() override;
  std::string GetCodeName() override;
  std::optional<bool> HasSensor(SensorType sensor) override;

 private:
  // Unowned pointer. The CrosConfigInterface should outlive this instance.
  CrosConfig* cros_config_;
  // Unowned pointer. The debugdProxyInterface should outlive this instance.
  org::chromium::debugdProxyInterface* debugd_proxy_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_
