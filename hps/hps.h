// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Main HPS class.
 */
#ifndef HPS_HPS_H_
#define HPS_HPS_H_

#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>

#include "hps/dev.h"
#include "hps/hps_metrics.h"

namespace hps {

class HPS {
 public:
  explicit HPS(std::unique_ptr<DevInterface> dev)
      : device_(std::move(dev)),
        state_(State::kBoot),
        retries_(0),
        reboots_(0),
        hw_rev_(0),
        appl_version_(0),
        feat_enabled_(0) {}
  // Set the application version and firmware.
  void Init(uint16_t appl_version,
            const base::FilePath& mcu,
            const base::FilePath& spi);
  /*
   * Boot the module, returns true if the module is working and ready.
   * Requires that the MCU and SPI flash blobs have been
   * set via Init().
   */
  bool Boot();
  /*
   * Skip the boot sequence, and assume the module is ready for
   * feature processing. Required if the module is running
   * application code without stage 0 RO and RW boot code.
   */
  void SkipBoot();
  /*
   * Enable the selected feature, return false if the
   * request fails e.g if the module is not ready.
   * The feature is represented as a feature index
   * starting from 0, with a current maximum of 15.
   */
  bool Enable(uint8_t feature);
  /*
   * Disable the selected feature.
   */
  bool Disable(uint8_t feature);
  /*
   * Return the latest result for the feature selected,
   * where the feature ranges from 0 to 15, corresponding to
   * the features selected in the Enable method above.
   * Returns -1 in the event the module is unavailable, or
   * the feature is not selected, or the result is not valid.
   */
  int Result(int feature);
  /*
   * Return the underlying access device for the module.
   */
  DevInterface* Device() { return this->device_.get(); }
  /*
   * Download a file to the bank indicated.
   * Per the HPS/Host I2C Interface, the bank
   * must be between 0-63 inclusive.
   * Returns true on success, false on failure.
   */
  bool Download(int bank, const base::FilePath& source);

 private:
  // Boot states.
  enum State {
    kBoot,
    kBootCheckFault,
    kBootOK,
    kUpdateAppl,
    kUpdateSpi,
    kStage1,
    kSpiVerify,
    kApplWait,
    kFailed,
    kReady,
  };
  void HandleState();
  void Reboot(const char* msg);
  void Fault();
  void Go(State newstate);
  bool WaitForBankReady(int bank);
  bool WriteFile(int bank, const base::FilePath& source);
  std::unique_ptr<DevInterface> device_;
  HpsMetrics hps_metrics_;
  base::Lock lock_;  // Exclusive module access lock
  State state_;      // Current state
  int retries_;      // Common retry counter for states.
  int reboots_;      // Count of reboots.
  uint16_t hw_rev_;
  uint16_t appl_version_;
  uint16_t feat_enabled_;
  base::FilePath mcu_blob_;
  base::FilePath spi_blob_;
};

}  // namespace hps

#endif  // HPS_HPS_H_
