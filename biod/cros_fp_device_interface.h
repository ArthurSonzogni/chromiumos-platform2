// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_DEVICE_INTERFACE_H_
#define BIOD_CROS_FP_DEVICE_INTERFACE_H_

#include <bitset>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <brillo/secure_blob.h>
#include <chromeos/ec/ec_commands.h>

#include "libec/ec_command.h"
#include "libec/ec_command_version_supported.h"
#include "libec/fingerprint/fp_mode.h"
#include "libec/fingerprint/fp_sensor_errors.h"

/**
 * Though it's nice to have the template as a SecureVector, for some templates
 * this will hit the RLIMIT_MEMLOCK and cause a crash. Since the template is
 * encrypted by the FPMCU, it's not strictly necessary to use SecureVector.
 */
using VendorTemplate = std::vector<uint8_t>;

namespace biod {

class CrosFpDeviceInterface : public ec::EcCommandVersionSupportedInterface {
 public:
  using MkbpCallback = base::RepeatingCallback<void(const uint32_t event)>;
  CrosFpDeviceInterface() = default;
  CrosFpDeviceInterface(const CrosFpDeviceInterface&) = delete;
  CrosFpDeviceInterface& operator=(const CrosFpDeviceInterface&) = delete;

  virtual ~CrosFpDeviceInterface() = default;

  struct EcVersion {
    std::string ro_version;
    std::string rw_version;
    ec_image current_image = EC_IMAGE_UNKNOWN;
  };

  virtual void SetMkbpEventCallback(MkbpCallback callback) = 0;

  struct FpStats {
    uint32_t capture_ms = 0;
    uint32_t matcher_ms = 0;
    uint32_t overall_ms = 0;
  };

  struct GetSecretReply {
    brillo::Blob encrypted_secret;
    brillo::Blob iv;
    brillo::Blob pk_out_x;
    brillo::Blob pk_out_y;
  };

  struct PairingKeyKeygenReply {
    brillo::Blob pub_x;
    brillo::Blob pub_y;
    brillo::Blob encrypted_private_key;
  };

  virtual bool SetFpMode(const ec::FpMode& mode) = 0;
  /**
   * @return mode on success, FpMode(FpMode::Mode::kModeInvalid) on failure
   */
  virtual ec::FpMode GetFpMode() = 0;
  virtual std::optional<FpStats> GetFpStats() = 0;
  virtual std::optional<std::bitset<32>> GetDirtyMap() = 0;
  virtual bool SupportsPositiveMatchSecret() = 0;
  virtual std::optional<brillo::SecureVector> GetPositiveMatchSecret(
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

  virtual ec::FpSensorErrors GetHwErrors() = 0;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_INTERFACE_H_
