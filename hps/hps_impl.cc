// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Main HPS class.

#include <fstream>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

#include "hps/hps_impl.h"
#include "hps/hps_reg.h"

namespace hps {

// Observed times are 2.5ms for a normal write, and 250ms for a block erase
// write. Set the sleep to 1/5 of the normal time, and the timeout to 10x the
// expected max time.
static constexpr base::TimeDelta kBankReadySleep =
    base::TimeDelta::FromMicroseconds(500);
static constexpr base::TimeDelta kBankReadyTimeout =
    base::TimeDelta::FromMilliseconds(2500);

static constexpr base::TimeDelta kMagicTimeout =
    base::TimeDelta::FromMilliseconds(1000);
static constexpr base::TimeDelta kMagicSleep =
    base::TimeDelta::FromMilliseconds(100);

// Initialise the firmware parameters.
void HPS_impl::Init(uint32_t appl_version,
                    const base::FilePath& mcu,
                    const base::FilePath& spi) {
  this->appl_version_ = appl_version;
  this->mcu_blob_ = mcu;
  this->spi_blob_ = spi;
}

// Attempt the boot sequence
// returns true if booting completed
bool HPS_impl::Boot() {
  // Make sure blobs are set etc.
  if (this->mcu_blob_.empty() || this->spi_blob_.empty()) {
    LOG(ERROR) << "No HPS firmware to download.";
    return false;
  }

  // TODO(evanbenn) we can't reboot here while we are using stm bootloader
  // this->Reboot();

  // If the boot process sent an update, reboot and try again
  // A full update takes 3 boots, so try 3 times.
  for (int i = 0; i < 3; ++i) {
    switch (this->TryBoot()) {
      case BootResult::kOk:
        return true;
      case BootResult::kFail:
        return false;
      case BootResult::kUpdate:
        LOG(INFO) << "Update sent, rebooting";
        this->Reboot();
        continue;
    }
  }
  LOG(FATAL) << "Boot failure, too many updates.";
  return false;
}

bool HPS_impl::Enable(uint8_t feature) {
  // Only 2 features available at the moment.
  if (feature >= kFeatures) {
    LOG(ERROR) << "Enabling unknown feature (" << feature << ")";
    return false;
  }
  // Check the application is enabled and running.
  int status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (status < 0 || (status & R2::kAppl) == 0) {
    LOG(ERROR) << "Module not ready for feature control";
    return false;
  }
  this->feat_enabled_ |= 1 << feature;
  // Write the enable feature mask.
  return this->device_->WriteReg(HpsReg::kFeatEn, this->feat_enabled_);
}

bool HPS_impl::Disable(uint8_t feature) {
  if (feature >= kFeatures) {
    LOG(ERROR) << "Disabling unknown feature (" << feature << ")";
    return false;
  }
  // Check the application is enabled and running.
  int status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (status < 0 || (status & R2::kAppl) == 0) {
    LOG(ERROR) << "Module not ready for feature control";
    return false;
  }
  this->feat_enabled_ &= ~(1 << feature);
  // Write the enable feature mask.
  return this->device_->WriteReg(HpsReg::kFeatEn, this->feat_enabled_);
}

FeatureResult HPS_impl::Result(int feature) {
  // Check the application is enabled and running.
  int status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (status < 0 || (status & R2::kAppl) == 0) {
    return {.valid = false};
  }
  // Check that feature is enabled.
  if (((1 << feature) & this->feat_enabled_) == 0) {
    return {.valid = false};
  }
  int hps_result;
  switch (feature) {
    case 0:
      hps_result = this->device_->ReadReg(HpsReg::kFeature0);
      break;
    case 1:
      hps_result = this->device_->ReadReg(HpsReg::kFeature1);
      break;
    default:
      hps_result = -1;
  }
  if (hps_result < 0) {
    return {.valid = false};
  }
  // TODO(slangley): Clean this up when we introduce sequence numbers for
  // inference results.
  FeatureResult result;
  result.valid = (hps_result & RFeat::kValid) == RFeat::kValid;

  // The lower 8 bits are an int8_t.
  // We are extracting that byte here, not converting the uint16_t.
  result.inference_result = static_cast<int8_t>(hps_result & 0xFF);
  return result;
}

// Attempt the boot sequence:
// Check stage0 flags, send a MCU update or continue
// Check stage1 flags, send a SPI update or continue
// Check stage2 flags, return result.
// returns BootResult::kOk if booting completed
// returns BootResult::kUpdate if an update was sent
// else returns BootResult::kFail
hps::HPS_impl::BootResult HPS_impl::TryBoot() {
  // Inspect stage0 flags and either fail, update, or launch stage1 and continue
  switch (this->CheckStage0()) {
    case BootResult::kOk:
      this->device_->WriteReg(HpsReg::kSysCmd, R3::kLaunch1);
      break;
    case BootResult::kFail:
      return BootResult::kFail;
    case BootResult::kUpdate:
      LOG(INFO) << "Updating MCU flash";
      if (this->WriteFile(0, this->mcu_blob_)) {
        return BootResult::kUpdate;
      } else {
        return BootResult::kFail;
      }
  }

  // Inspect stage1 flags and either fail, update, or launch stage2 and continue
  switch (this->CheckStage1()) {
    case BootResult::kOk:
      this->device_->WriteReg(HpsReg::kSysCmd, R3::kLaunchAppl);
      break;
    case BootResult::kFail:
      return BootResult::kFail;
    case BootResult::kUpdate:
      LOG(INFO) << "Updating SPI flash";
      if (this->WriteFile(1, this->spi_blob_)) {
        return BootResult::kUpdate;
      } else {
        return BootResult::kFail;
      }
  }

  // Inspect stage2 flags for success or failure
  return this->CheckStage2();
}

// Returns true if the device replies with the expected magic number in time.
// Attempts are made for kMagicTimeout time, with kMagicSleep delays between
// failures. Retries are only done for failed reads, not incorrect
// responses.
bool HPS_impl::CheckMagic() {
  base::ElapsedTimer timer;
  for (;;) {
    int magic = this->device_->ReadReg(HpsReg::kMagic);
    if (magic < 0) {
      if (timer.Elapsed() < kMagicTimeout) {
        base::PlatformThread::Sleep(kMagicSleep);
        continue;
      } else {
        LOG(FATAL) << "Timeout waiting for boot magic number";
        return false;
      }
    } else if (magic == kHpsMagic) {
      VLOG(1) << "Good magic number";
      return true;
    } else {
      hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kBadMagic);
      LOG(FATAL) << base::StringPrintf("Bad magic number 0x%04x", magic);
      return false;
    }
  }
}

// Check stage0 status:
// Check status flags.
// Read and store kHwRev.
// Read and store kWpOff.
// Check stage1 verification and version.
// Return BootResult::kOk if booting should continue.
// Return BootResult::kUpdate if an update should be sent.
// Else return BootResult::kFail.
hps::HPS_impl::BootResult HPS_impl::CheckStage0() {
  if (!CheckMagic()) {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kNoResponse);
    return BootResult::kFail;
  }

  int status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (status < 0) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "ReadReg failure";
    return BootResult::kFail;
  }

  if (status & R2::kFault || !(status & R2::kOK)) {
    this->Fault();
    return BootResult::kFail;
  }

  int hwrev = this->device_->ReadReg(HpsReg::kHwRev);
  if (hwrev >= 0) {
    this->hw_rev_ = base::checked_cast<uint16_t>(hwrev);
  } else {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "Failed to read hwrev";
    return BootResult::kFail;
  }

  this->write_protect_off_ = status & R2::kWpOff;
  VLOG_IF(1, this->write_protect_off_) << "kWpOff, ignoring verified bits";

  // Send an update only when WP is off and there is no verified signal
  if (!this->write_protect_off_ && !(status & R2::kApplVerified)) {
    // Appl not verified, so need to update it.
    LOG(INFO) << "Appl flash not verified";
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kMcuNotVerified);
    return BootResult::kUpdate;
  }

  // Verified, so now check the version. If it is different, update it.
  int version_low = this->device_->ReadReg(HpsReg::kFirmwareVersionLow);
  int version_high = this->device_->ReadReg(HpsReg::kFirmwareVersionHigh);
  if (version_low < 0 || version_high < 0) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "ReadReg failure";
    return BootResult::kFail;
  }
  uint32_t version = static_cast<uint32_t>((version_high << 16) |
                                           static_cast<uint16_t>(version_low));
  if (version == this->appl_version_) {
    // Application is verified, launch it.
    VLOG(1) << "Appl version OK";
    return BootResult::kOk;
  } else {
    // Versions do not match, need to update.
    LOG(INFO) << "Appl version mismatch, module: " << version
              << " expected: " << this->appl_version_;
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kMcuVersionMismatch);
    return BootResult::kUpdate;
  }
}

// Check stage1 status:
// Check status flags.
// Check spi verification.
// Return BootResult::kOk if booting should continue.
// Return BootResult::kUpdate if an update should be sent.
// Else return BootResult::kFail.
hps::HPS_impl::BootResult HPS_impl::CheckStage1() {
  if (!CheckMagic()) {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kStage1NotStarted);
    return BootResult::kFail;
  }

  int status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (status < 0) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "ReadReg failure";
    return BootResult::kFail;
  }

  if (status & R2::kFault || !(status & R2::kOK)) {
    this->Fault();
    return BootResult::kFail;
  }

  if (!(status & R2::kStage1)) {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kStage1NotStarted);
    LOG(FATAL) << "Stage 1 did not start";
    return BootResult::kFail;
  }

  // Send an update only when WP is on and there is no verified signal
  if (!this->write_protect_off_ && !(status & R2::kSpiVerified)) {
    LOG(INFO) << "SPI flash not verified";
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kSpiNotVerified);
    return BootResult::kUpdate;
  } else {
    VLOG(1) << "Enabling application";
    return BootResult::kOk;
  }
}

// Check stage2 status:
// Check status flags.
// Return BootResult::kOk if booting should continue.
// Return BootResult::kUpdate if an update should be sent.
// Else returns BootResult::kFail.
hps::HPS_impl::BootResult HPS_impl::CheckStage2() {
  if (!CheckMagic()) {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kApplNotStarted);
    return BootResult::kFail;
  }

  int status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (status < 0) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "ReadReg failure";
    return BootResult::kFail;
  }

  if (status & R2::kFault || !(status & R2::kOK)) {
    this->Fault();
    return BootResult::kFail;
  }

  if (!(status & R2::kAppl)) {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kApplNotStarted);
    LOG(FATAL) << "Application did not start";
    return BootResult::kFail;
  } else {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kSuccess);
    return BootResult::kOk;
  }
}

// Reboot the hardware module.
void HPS_impl::Reboot() {
  LOG(INFO) << "Rebooting";
  // Send a reset cmd - maybe should power cycle.
  this->device_->WriteReg(HpsReg::kSysCmd, R3::kReset);
}

// Fault bit seen, attempt to dump status information
void HPS_impl::Fault() {
  hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kFault);
  int errors = this->device_->ReadReg(HpsReg::kError);
  if (errors < 0) {
    LOG(FATAL) << "Fault: cause unknown";
  } else {
    LOG(FATAL) << base::StringPrintf("Fault: cause 0x%04x",
                                     static_cast<unsigned>(errors))
                      .c_str();
  }
}

/*
 * Download data to the bank specified.
 * The HPS/Host I2C Interface Memory Write is used.
 */
bool HPS_impl::Download(hps::HpsBank bank, const base::FilePath& source) {
  uint8_t ibank = static_cast<uint8_t>(bank);
  if (ibank >= kNumBanks) {
    LOG(ERROR) << "Download: Illegal bank: " << ibank << ": " << source;
    return -1;
  }
  return this->WriteFile(ibank, source);
}

void HPS_impl::SetDownloadObserver(DownloadObserver observer) {
  this->download_observer_ = std::move(observer);
}

/*
 * Write the file to the bank indicated.
 */
bool HPS_impl::WriteFile(uint8_t bank, const base::FilePath& source) {
  base::File file(source,
                  base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  if (!file.IsValid()) {
    LOG(ERROR) << "WriteFile: " << source << ": "
               << base::File::ErrorToString(file.error_details());
    return false;
  }
  uint64_t bytes = 0;
  int64_t total_bytes = file.GetLength();
  uint32_t address = 0;
  int rd;
  base::TimeTicks start_time = base::TimeTicks::Now();
  /*
   * Leave room for a 32 bit address at the start of the block to be written.
   * The address is updated for each block to indicate
   * where this block is to be written.
   * The format of the data block is:
   *    4 bytes of address in big endian format
   *    data
   */
  auto buf = std::make_unique<uint8_t[]>(this->device_->BlockSizeBytes() +
                                         sizeof(uint32_t));

  do {
    if (!this->WaitForBankReady(bank)) {
      LOG(ERROR) << "WriteFile: bank not ready: " << bank;
      return false;
    }
    buf[0] = address >> 24;
    buf[1] = (address >> 16) & 0xff;
    buf[2] = (address >> 8) & 0xff;
    buf[3] = address & 0xff;
    rd = file.ReadAtCurrentPos(
        reinterpret_cast<char*>(&buf[sizeof(uint32_t)]),
        static_cast<int>(this->device_->BlockSizeBytes()));
    if (rd > 0) {
      if (!this->device_->Write(I2cMemWrite(bank), &buf[0],
                                static_cast<size_t>(rd) + sizeof(uint32_t))) {
        LOG(ERROR) << "WriteFile: device write error. bank: " << bank;
        return false;
      }
      address += static_cast<uint32_t>(rd);
      bytes += static_cast<uint64_t>(rd);
      if (download_observer_) {
        download_observer_.Run(source, static_cast<uint32_t>(total_bytes),
                               bytes, base::TimeTicks::Now() - start_time);
      }
    }
  } while (rd > 0);  // A read returning 0 indicates EOF.
  VLOG(1) << "Wrote " << bytes << " bytes from " << source;
  // Wait for the bank to become ready again to ensure the write is complete.
  this->WaitForBankReady(bank);
  return true;
}

bool HPS_impl::WaitForBankReady(uint8_t bank) {
  base::ElapsedTimer timer;
  do {
    int result = this->device_->ReadReg(HpsReg::kBankReady);
    if (result < 0) {
      return false;
    }
    if (result & (1 << bank)) {
      return true;
    }
    base::PlatformThread::Sleep(kBankReadySleep);
  } while (timer.Elapsed() < kBankReadyTimeout);
  return false;
}

}  // namespace hps
