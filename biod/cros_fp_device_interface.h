// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_DEVICE_INTERFACE_H_
#define BIOD_CROS_FP_DEVICE_INTERFACE_H_

#include <bitset>
#include <string>
#include <vector>

#include <base/message_loop/message_loop.h>
#include <brillo/secure_blob.h>
#include <chromeos/ec/ec_commands.h>

#include "biod/ec_command.h"
#include "biod/fp_mode.h"

using MessageLoopForIO = base::MessageLoopForIO;

using VendorTemplate = std::vector<uint8_t>;

namespace biod {

class CrosFpDeviceInterface {
 public:
  CrosFpDeviceInterface() = default;
  virtual ~CrosFpDeviceInterface() = default;

  struct EcVersion {
    std::string ro_version;
    std::string rw_version;
    ec_current_image current_image = EC_IMAGE_UNKNOWN;
  };

  virtual bool SetFpMode(const FpMode& mode) = 0;
  virtual bool GetFpMode(FpMode* mode) = 0;
  virtual bool GetFpStats(int* capture_ms,
                          int* matcher_ms,
                          int* overall_ms) = 0;
  virtual bool GetDirtyMap(std::bitset<32>* bitmap) = 0;
  virtual bool SupportsPositiveMatchSecret() = 0;
  virtual bool GetPositiveMatchSecret(int index,
                                      brillo::SecureBlob* secret) = 0;
  virtual bool GetTemplate(int index, VendorTemplate* out) = 0;
  virtual bool UploadTemplate(const VendorTemplate& tmpl) = 0;
  virtual bool SetContext(std::string user_id) = 0;
  virtual bool ResetContext() = 0;
  // Initialise the entropy in the SBP. If |reset| is true, the old entropy
  // will be deleted. If |reset| is false, we will only add entropy, and only
  // if no entropy had been added before.
  virtual bool InitEntropy(bool reset) = 0;

  virtual int MaxTemplateCount() = 0;
  virtual int TemplateVersion() = 0;

  virtual EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                          uint32_t ver) = 0;

  DISALLOW_COPY_AND_ASSIGN(CrosFpDeviceInterface);
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_INTERFACE_H_
