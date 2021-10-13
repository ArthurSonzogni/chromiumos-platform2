// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Implementation of the HPS interface.
 */
#ifndef HPS_HPS_IMPL_H_
#define HPS_HPS_IMPL_H_

#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>

#include "hps/dev.h"
#include "hps/hps.h"
#include "hps/hps_metrics.h"
#include "hps/hps_reg.h"

namespace hps {

class HPS_impl : public HPS {
 public:
  explicit HPS_impl(std::unique_ptr<DevInterface> dev)
      : device_(std::move(dev)),
        state_(State::kBoot),
        retries_(0),
        reboots_(0),
        hw_rev_(0),
        appl_version_(0),
        write_protect_off_(false),
        feat_enabled_(0) {}

  // Methods for HPS
  void Init(uint32_t appl_version,
            const base::FilePath& mcu,
            const base::FilePath& spi) override;
  bool Boot() override;
  void SkipBoot() override;
  bool Enable(uint8_t feature) override;
  bool Disable(uint8_t feature) override;
  FeatureResult Result(int feature) override;
  DevInterface* Device() override { return this->device_.get(); }
  bool Download(hps::HpsBank bank, const base::FilePath& source) override;
  void SetDownloadObserver(DownloadObserver) override;

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
    hps_metrics_.SetMetricsLibraryForTesting(std::move(metrics_lib));
  }

  MetricsLibraryInterface* metrics_library_for_testing() {
    return hps_metrics_.metrics_library_for_testing();
  }

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
  uint32_t appl_version_;
  bool write_protect_off_;
  uint16_t feat_enabled_;
  base::FilePath mcu_blob_;
  base::FilePath spi_blob_;
  DownloadObserver download_observer_{};
};

}  // namespace hps

#endif  // HPS_HPS_IMPL_H_
