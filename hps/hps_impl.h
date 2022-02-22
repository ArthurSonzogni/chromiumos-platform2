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
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/threading/thread.h>

#include "hps/dev.h"
#include "hps/hps.h"
#include "hps/hps_metrics.h"
#include "hps/hps_reg.h"

namespace hps {

class HPS_impl : public HPS {
 public:
  friend class HPSTestButUsingAMock;
  explicit HPS_impl(std::unique_ptr<DevInterface> dev)
      : device_(std::move(dev)),
        wake_lock_(device_->CreateWakeLock()),  // Power on by default.
        hw_rev_(0),
        stage1_version_(0),
        write_protect_off_(false),
        feat_enabled_(0) {}

  // Methods for HPS
  void Init(uint32_t stage1_version,
            const base::FilePath& mcu,
            const base::FilePath& fpga_bitstream,
            const base::FilePath& fpga_app_image) override;
  bool Boot() override;
  bool ShutDown() override;
  bool IsRunning() override;
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
  enum class BootResult {
    kFail,
    kOk,
    kUpdate,
  };

  // This is is virtual to allow unit tests to override
  virtual void Sleep(base::TimeDelta duration) {
    base::PlatformThread::Sleep(duration);
  }
  BootResult TryBoot();
  bool CheckMagic();
  BootResult CheckStage0();
  BootResult CheckStage1();
  BootResult CheckApplication();
  bool Reboot();
  [[noreturn]] void OnBootFault(const base::Location&);
  [[noreturn]] void OnFatalError(const base::Location&, const std::string& msg);
  bool WaitForBankReady(uint8_t bank);
  BootResult SendStage1Update();
  BootResult SendApplicationUpdate();
  bool WriteFile(uint8_t bank, const base::FilePath& source);
  std::unique_ptr<DevInterface> device_;
  base::TimeTicks boot_start_time_;
  std::unique_ptr<WakeLock> wake_lock_;
  HpsMetrics hps_metrics_;
  uint16_t hw_rev_;
  uint32_t stage1_version_;
  bool write_protect_off_;
  uint16_t feat_enabled_;
  base::FilePath mcu_blob_;
  base::FilePath fpga_bitstream_;
  base::FilePath fpga_app_image_;
  DownloadObserver download_observer_{};
};

}  // namespace hps

#endif  // HPS_HPS_IMPL_H_
