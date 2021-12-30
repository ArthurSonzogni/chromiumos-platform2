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
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

#include "hps/hps_impl.h"
#include "hps/hps_reg.h"

namespace hps {

// Observed times are
// MCU: ~4ms for a normal write, ~27ms for a erase write
// SPI: 3ms for a normal write, 250ms for a erase write
// 5000ms for the full erase
// Theoretical max time for SPI flash full erase is 120s
// Set the sleep to ~1/5 of the normal time, and the timeout to 2x the
// expected max time. TODO(evanbenn) only do the long timeout for the
// first spi write.
static constexpr base::TimeDelta kBankReadySleep =
    base::TimeDelta::FromMicroseconds(500);
static constexpr base::TimeDelta kBankReadyTimeout =
    base::TimeDelta::FromSeconds(240);

// After reset, we poll the magic number register for this long.
// Observed time is 1000ms.
static constexpr base::TimeDelta kMagicSleep =
    base::TimeDelta::FromMilliseconds(100);
static constexpr base::TimeDelta kMagicTimeout =
    base::TimeDelta::FromMilliseconds(3000);

// After requesting application launch, we must wait for verification
// Observed time is 100 seconds.
static constexpr base::TimeDelta kApplTimeout =
    base::TimeDelta::FromMilliseconds(200000);
static constexpr base::TimeDelta kApplSleep =
    base::TimeDelta::FromMilliseconds(1000);

// Initialise the firmware parameters.
void HPS_impl::Init(uint32_t stage1_version,
                    const base::FilePath& mcu,
                    const base::FilePath& fpga_bitstream,
                    const base::FilePath& fpga_app_image) {
  this->stage1_version_ = stage1_version;
  this->mcu_blob_ = mcu;
  this->fpga_bitstream_ = fpga_bitstream;
  this->fpga_app_image_ = fpga_app_image;
}

// Attempt the boot sequence
// returns true if booting completed
bool HPS_impl::Boot() {
  // Make sure blobs are set etc.
  if (this->mcu_blob_.empty() || this->fpga_bitstream_.empty() ||
      this->fpga_app_image_.empty()) {
    LOG(ERROR) << "No HPS firmware to download.";
    return false;
  }

  this->Reboot();

  // If the boot process sent an update, reboot and try again
  // A full update takes 3 boots, so try 3 times.
  for (int i = 0; i < 3; ++i) {
    switch (this->TryBoot()) {
      case BootResult::kOk:
        LOG(INFO) << "HPS device booted";
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
    LOG(ERROR) << "Enabling unknown feature (" << static_cast<int>(feature)
               << ")";
    return false;
  }
  // Check the application is enabled and running.
  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status || !(status.value() & R2::kAppl)) {
    LOG(ERROR) << "Module not ready for feature control";
    return false;
  }
  this->feat_enabled_ |= 1 << feature;
  // Write the enable feature mask.
  return this->device_->WriteReg(HpsReg::kFeatEn, this->feat_enabled_);
}

bool HPS_impl::Disable(uint8_t feature) {
  if (feature >= kFeatures) {
    LOG(ERROR) << "Disabling unknown feature (" << static_cast<int>(feature)
               << ")";
    return false;
  }
  // Check the application is enabled and running.
  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status || !(status.value() & R2::kAppl)) {
    LOG(ERROR) << "Module not ready for feature control";
    return false;
  }
  this->feat_enabled_ &= ~(1 << feature);
  // Write the enable feature mask.
  return this->device_->WriteReg(HpsReg::kFeatEn, this->feat_enabled_);
}

FeatureResult HPS_impl::Result(int feature) {
  // Check the application is enabled and running.
  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status || !(status.value() & R2::kAppl)) {
    return {.valid = false};
  }
  // Check that feature is enabled.
  if (((1 << feature) & this->feat_enabled_) == 0) {
    return {.valid = false};
  }
  std::optional<uint16_t> hps_result = std::nullopt;
  switch (feature) {
    case 0:
      hps_result = this->device_->ReadReg(HpsReg::kFeature0);
      break;
    case 1:
      hps_result = this->device_->ReadReg(HpsReg::kFeature1);
      break;
  }
  if (!hps_result) {
    return {.valid = false};
  }
  // TODO(slangley): Clean this up when we introduce sequence numbers for
  // inference results.
  FeatureResult result;
  result.valid = (hps_result.value() & RFeat::kValid) == RFeat::kValid;

  // The lower 8 bits are an int8_t.
  // We are extracting that byte here, not converting the uint16_t.
  result.inference_result = static_cast<int8_t>(hps_result.value() & 0xFF);
  return result;
}

// Attempt the boot sequence:
// Check stage0 flags, send a MCU update, fail or continue
// Check stage1 flags, fail or continue
// Check stage2 flags, send a SPI update or continue
// returns BootResult::kOk if booting completed
// returns BootResult::kUpdate if an update was sent
// else returns BootResult::kFail
hps::HPS_impl::BootResult HPS_impl::TryBoot() {
  // Inspect stage0 flags and either fail, update, or launch stage1 and continue
  switch (this->CheckStage0()) {
    case BootResult::kOk:
      VLOG(1) << "Launching stage 1";
      if (!this->device_->WriteReg(HpsReg::kSysCmd, R3::kLaunch1)) {
        LOG(FATAL) << "Launch stage 1 failed";
      }
      break;
    case BootResult::kFail:
      return BootResult::kFail;
    case BootResult::kUpdate:
      return SendStage1Update();
  }

  // Inspect stage1 flags and either fail or launch application and continue
  switch (this->CheckStage1()) {
    case BootResult::kOk:
      VLOG(1) << "Launching Application";
      if (!this->device_->WriteReg(HpsReg::kSysCmd, R3::kLaunchAppl)) {
        LOG(FATAL) << "Launch Application failed";
      }
      break;
    case BootResult::kFail:
    case BootResult::kUpdate:
      return BootResult::kFail;
  }

  // Inspect application flags and either fail, send an update, or succeed
  switch (this->CheckApplication()) {
    case BootResult::kOk:
      VLOG(1) << "Application Running";
      return BootResult::kOk;
    case BootResult::kFail:
      return BootResult::kFail;
    case BootResult::kUpdate:
      return SendApplicationUpdate();
  }
}

// Returns true if the device replies with the expected magic number in time.
// Attempts are made for kMagicTimeout time, with kMagicSleep delays between
// failures. Retries are only done for failed reads, not incorrect
// responses.
bool HPS_impl::CheckMagic() {
  base::ElapsedTimer timer;
  for (;;) {
    std::optional<uint16_t> magic = this->device_->ReadReg(HpsReg::kMagic);
    if (!magic) {
      if (timer.Elapsed() < kMagicTimeout) {
        Sleep(kMagicSleep);
        continue;
      } else {
        LOG(FATAL) << "Timeout waiting for boot magic number";
        return false;
      }
    } else if (magic == kHpsMagic) {
      VLOG(1) << "Good magic number after " << timer.Elapsed().InMilliseconds()
              << "ms";
      return true;
    } else {
      hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kBadMagic);
      LOG(FATAL) << base::StringPrintf("Bad magic number 0x%04x",
                                       magic.value());
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

  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "ReadReg failure";
    return BootResult::kFail;
  }

  if (status.value() & R2::kFault || !(status.value() & R2::kOK)) {
    this->Fault();
    return BootResult::kFail;
  }

  std::optional<uint16_t> hwrev = this->device_->ReadReg(HpsReg::kHwRev);
  if (!hwrev) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "Failed to read hwrev";
    return BootResult::kFail;
  }
  this->hw_rev_ = hwrev.value();

  this->write_protect_off_ = status.value() & R2::kWpOff;
  VLOG_IF(1, this->write_protect_off_) << "kWpOff, ignoring verified bits";

  // When write protect is off we ignore the verified signal.
  // When write protect is not off we update if there is no verified signal.
  if (!this->write_protect_off_ && !(status.value() & R2::kStage1Verified)) {
    // Stage1 not verified, so need to update it.
    LOG(INFO) << "Stage1 flash not verified";
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kMcuNotVerified);
    return BootResult::kUpdate;
  }

  // Verified, so now check the version. If it is different, update it.
  std::optional<uint16_t> version_low =
      this->device_->ReadReg(HpsReg::kFirmwareVersionLow);
  std::optional<uint16_t> version_high =
      this->device_->ReadReg(HpsReg::kFirmwareVersionHigh);
  if (!version_low || !version_high) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "ReadReg failure";
    return BootResult::kFail;
  }
  uint32_t version =
      static_cast<uint32_t>(version_high.value() << 16) | version_low.value();
  if (version == this->stage1_version_) {
    // Stage 1 is verified
    VLOG(1) << "Stage1 version OK";
    return BootResult::kOk;
  } else {
    // Versions do not match, need to update.
    LOG(INFO) << "Stage1 version mismatch, module: " << version
              << " expected: " << this->stage1_version_;
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

  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status) {
    // TODO(evanbenn) log a metric
    LOG(FATAL) << "ReadReg failure";
    return BootResult::kFail;
  }

  if (status.value() & R2::kFault || !(status.value() & R2::kOK)) {
    this->Fault();
    return BootResult::kFail;
  }

  if (!(status.value() & R2::kStage1)) {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kStage1NotStarted);
    LOG(FATAL) << "Stage 1 did not start";
    return BootResult::kFail;
  }
  VLOG(1) << "Stage 1 OK";
  return BootResult::kOk;
}

// Check stage2 status:
// Check status flags.
// Return BootResult::kOk if application is running.
// Return BootResult::kUpdate if an update should be sent.
// Else returns BootResult::kFail.
hps::HPS_impl::BootResult HPS_impl::CheckApplication() {
  // Poll for kAppl (started) or kSpiNotVer (not started)
  base::ElapsedTimer timer;
  do {
    std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
    if (!status) {
      // TODO(evanbenn) log a metric
      LOG(FATAL) << "ReadReg failure";
      return BootResult::kFail;
    }
    if (status.value() & R2::kAppl) {
      VLOG(1) << "Application boot after " << timer.Elapsed().InMilliseconds()
              << "ms";
      hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kSuccess);
      return BootResult::kOk;
    }

    std::optional<uint16_t> error = this->device_->ReadReg(HpsReg::kError);
    if (!error) {
      // TODO(evanbenn) log a metric
      LOG(FATAL) << "ReadReg failure";
      return BootResult::kFail;
    }
    if (error.value() & RError::kSpiNotVer) {
      VLOG(1) << "SPI verification failed after "
              << timer.Elapsed().InMilliseconds() << "ms";
      hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kSpiNotVerified);
      return BootResult::kUpdate;
    } else if (error.value()) {
      this->Fault();
      return BootResult::kFail;
    }

    Sleep(kApplSleep);
  } while (timer.Elapsed() < kApplTimeout);

  hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kApplNotStarted);
  LOG(FATAL) << "Application did not start";
  return BootResult::kFail;
}

// Reboot the hardware module.
bool HPS_impl::Reboot() {
  // Send a reset cmd - maybe should power cycle.
  if (!this->device_->WriteReg(HpsReg::kSysCmd, R3::kReset)) {
    LOG(FATAL) << "Reboot failed";
    return false;
  }
  return true;
}

// Fault bit seen, attempt to dump status information
void HPS_impl::Fault() {
  hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kFault);
  std::optional<uint16_t> errors = this->device_->ReadReg(HpsReg::kError);
  if (!errors) {
    LOG(FATAL) << "Fault: cause unknown";
  } else {
    LOG(FATAL) << base::StringPrintf("Fault: cause 0x%04x", errors.value());
  }
}

// Send the stage1 MCU flash update.
// Returns kFail or kUpdate.
hps::HPS_impl::BootResult HPS_impl::SendStage1Update() {
  LOG(INFO) << "Updating MCU flash";
  base::ElapsedTimer timer;
  if (this->Download(HpsBank::kMcuFlash, this->mcu_blob_)) {
    hps_metrics_.SendHpsUpdateDuration(HpsBank::kMcuFlash, timer.Elapsed());
    return BootResult::kUpdate;
  } else {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kMcuUpdateFailure);
    return BootResult::kFail;
  }
}

// Send the Application SPI flash update.
// Returns kFail or kUpdate.
hps::HPS_impl::BootResult HPS_impl::SendApplicationUpdate() {
  LOG(INFO) << "Updating SPI flash";
  base::ElapsedTimer timer;
  if (this->Download(HpsBank::kSpiFlash, this->fpga_bitstream_) &&
      this->Download(HpsBank::kSocRom, this->fpga_app_image_)) {
    hps_metrics_.SendHpsUpdateDuration(HpsBank::kSpiFlash, timer.Elapsed());
    return BootResult::kUpdate;
  } else {
    hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kSpiUpdateFailure);
    return BootResult::kFail;
  }
}

/*
 * Download data to the bank specified.
 * The HPS/Host I2C Interface Memory Write is used.
 */
bool HPS_impl::Download(hps::HpsBank bank, const base::FilePath& source) {
  uint8_t ibank = static_cast<uint8_t>(bank);
  if (ibank >= kNumBanks) {
    LOG(ERROR) << "Download: Illegal bank: " << static_cast<int>(ibank) << ": "
               << source;
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
    PLOG(ERROR) << "WriteFile: \"" << source << "\": Open failed: "
                << base::File::ErrorToString(file.error_details());
    return false;
  }
  uint64_t bytes = 0;
  int64_t total_bytes = file.GetLength();
  if (total_bytes < 0) {
    PLOG(ERROR) << "WriteFile: \"" << source << "\" GetLength failed: ";
    return false;
  }
  uint32_t address = 0;
  int rd;
  base::ElapsedTimer timer;
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
      LOG(ERROR) << "WriteFile: bank not ready: " << static_cast<int>(bank);
      return false;
    }
    buf[0] = address >> 24;
    buf[1] = (address >> 16) & 0xff;
    buf[2] = (address >> 8) & 0xff;
    buf[3] = address & 0xff;
    rd = file.ReadAtCurrentPos(
        reinterpret_cast<char*>(&buf[sizeof(uint32_t)]),
        static_cast<int>(this->device_->BlockSizeBytes()));
    if (rd < 0) {
      PLOG(ERROR) << "WriteFile: \"" << source << "\" Read failed: ";
      return false;
    }
    if (rd > 0) {
      if (!this->device_->Write(I2cMemWrite(bank), &buf[0],
                                static_cast<size_t>(rd) + sizeof(uint32_t))) {
        LOG(ERROR) << "WriteFile: device write error. bank: "
                   << static_cast<int>(bank);
        return false;
      }
      address += static_cast<uint32_t>(rd);
      bytes += static_cast<uint64_t>(rd);
      if (download_observer_) {
        download_observer_.Run(source, static_cast<uint32_t>(total_bytes),
                               bytes, timer.Elapsed());
      }
    }
  } while (rd > 0);  // A read returning 0 indicates EOF.
  VLOG(1) << "Wrote " << bytes << " bytes from " << source << " in "
          << timer.Elapsed().InMilliseconds() << "ms";
  return true;
}

bool HPS_impl::WaitForBankReady(uint8_t bank) {
  base::ElapsedTimer timer;
  do {
    std::optional<uint16_t> result = this->device_->ReadReg(HpsReg::kBankReady);
    if (!result) {
      return false;
    }
    if (result.value() & (1 << bank)) {
      return true;
    }
    Sleep(kBankReadySleep);
  } while (timer.Elapsed() < kBankReadyTimeout);
  return false;
}

}  // namespace hps
