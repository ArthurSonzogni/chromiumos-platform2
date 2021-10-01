// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_HPS_H_
#define HPS_HPS_H_

#include <base/files/file_path.h>

#include "hps/hps_reg.h"

namespace hps {

class DevInterface;

class HPS {
 public:
  virtual ~HPS() = default;

  // Set the application version and firmware.
  virtual void Init(uint32_t appl_version,
                    const base::FilePath& mcu,
                    const base::FilePath& spi) = 0;

  //
  // Boot the module, returns true if the module is working and ready.
  // Requires that the MCU and SPI flash blobs have been
  // set via Init().
  //
  virtual bool Boot() = 0;

  //
  // Skip the boot sequence, and assume the module is ready for
  // feature processing. Required if the module is running
  // application code without stage 0 RO and RW boot code.
  //
  virtual void SkipBoot() = 0;

  //
  // Enable the selected feature, return false if the
  // request fails e.g if the module is not ready.
  // The feature is represented as a feature index
  // starting from 0, with a current maximum of 15.
  //
  virtual bool Enable(uint8_t feature) = 0;

  //
  // Disable the selected feature.
  //
  virtual bool Disable(uint8_t feature) = 0;

  //
  // Return the latest result for the feature selected,
  // where the feature ranges from 0 to 15, corresponding to
  // the features selected in the Enable method above.
  //
  virtual FeatureResult Result(int feature) = 0;

  //
  // Return the underlying access device for the module.
  //
  virtual DevInterface* Device() = 0;

  //
  // Download a file to the bank indicated.
  // Per the HPS/Host I2C Interface, the bank
  // must be between 0-63 inclusive.
  // Returns true on success, false on failure.
  //
  virtual bool Download(hps::HpsBank bank, const base::FilePath& source) = 0;
};

}  // namespace hps

#endif  // HPS_HPS_H_
