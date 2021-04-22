// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Main HPS class.
 */
#ifndef HPS_LIB_HPS_H_
#define HPS_LIB_HPS_H_

#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

#include <base/files/file_path.h>

#include "hps/lib/dev.h"

namespace hps {

class HPS {
 public:
  explicit HPS(std::unique_ptr<DevInterface> dev)
      : device_(std::move(dev)),
        state_(State::kBoot),
        retries_(0),
        reboots_(0),
        hw_rev_(0),
        appl_version_(0) {}
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
  void Fail(const char* msg);
  void Reboot(const char* msg);
  void Fault();
  void Go(State newstate);
  bool WaitForBankReady(int bank);
  std::unique_ptr<DevInterface> device_;
  State state_;  // Current state
  int retries_;  // Common retry counter for states.
  int reboots_;  // Count of reboots.
  uint16_t hw_rev_;
  uint16_t appl_version_;
  base::FilePath mcu_blob_;
  base::FilePath spi_blob_;
};

}  // namespace hps

#endif  // HPS_LIB_HPS_H_
