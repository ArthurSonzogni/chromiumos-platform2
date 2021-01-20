// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_DEVICE_INTERFACE_H_
#define BIOD_CROS_FP_DEVICE_INTERFACE_H_

#include <bitset>
#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <chromeos/ec/ec_commands.h>

#include "biod/ec_command.h"
#include "biod/fp_mode.h"

/**
 * Though it's nice to have the template as a SecureVector, for some templates
 * this will hit the RLIMIT_MEMLOCK and cause a crash. Since the template is
 * encrypted by the FPMCU, it's not strictly necessary to use SecureVector.
 */
using VendorTemplate = std::vector<uint8_t>;

namespace biod {

class CrosFpDeviceInterface {
 public:
  using MkbpCallback = base::Callback<void(const uint32_t event)>;
  CrosFpDeviceInterface() = default;
  CrosFpDeviceInterface(const CrosFpDeviceInterface&) = delete;
  CrosFpDeviceInterface& operator=(const CrosFpDeviceInterface&) = delete;

  virtual ~CrosFpDeviceInterface() = default;

  struct EcVersion {
    std::string ro_version;
    std::string rw_version;
    ec_current_image current_image = EC_IMAGE_UNKNOWN;
  };

  virtual void SetMkbpEventCallback(MkbpCallback callback) = 0;

  struct FpStats {
    uint32_t capture_ms = 0;
    uint32_t matcher_ms = 0;
    uint32_t overall_ms = 0;
  };

  virtual bool SetFpMode(const FpMode& mode) = 0;
  /**
   * @return mode on success, FpMode(FpMode::Mode::kModeInvalid) on failure
   */
  virtual FpMode GetFpMode() = 0;
  virtual base::Optional<FpStats> GetFpStats() = 0;
  virtual base::Optional<std::bitset<32>> GetDirtyMap() = 0;
  virtual bool SupportsPositiveMatchSecret() = 0;
  virtual base::Optional<brillo::SecureVector> GetPositiveMatchSecret(
      int index) = 0;
  virtual std::unique_ptr<VendorTemplate> GetTemplate(int index) = 0;
  virtual bool UploadTemplate(const VendorTemplate& tmpl) = 0;
  virtual bool SetContext(std::string user_id) = 0;
  virtual bool ResetContext() = 0;
  // Initialise the entropy in the SBP. If |reset| is true, the old entropy
  // will be deleted. If |reset| is false, we will only add entropy, and only
  // if no entropy had been added before.
  virtual bool InitEntropy(bool reset) = 0;
  virtual bool UpdateFpInfo() = 0;

  virtual int MaxTemplateCount() = 0;
  virtual int TemplateVersion() = 0;
  virtual int DeadPixelCount() = 0;

  virtual EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                          uint32_t ver) = 0;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_INTERFACE_H_
