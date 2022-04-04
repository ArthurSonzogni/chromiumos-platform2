// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Main HPS class.

#include <algorithm>
#include <fstream>
#include <optional>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>
#include <lzma.h>

#include "hps/hps_impl.h"
#include "hps/hps_reg.h"
#include "hps/utils.h"

namespace hps {

// Observed times are
// MCU: ~4ms for a normal write, ~27ms for a erase write
// SPI: 3ms for a normal write, 250ms for a erase write
// 5000ms for the full erase
// Theoretical max time for SPI flash full erase is 120s
// Set the sleep to ~1/5 of the normal time, and the timeout to 2x the
// expected max time. TODO(evanbenn) only do the long timeout for the
// first spi write.
static constexpr base::TimeDelta kBankReadySleep = base::Microseconds(500);
static constexpr base::TimeDelta kBankReadyTimeout = base::Seconds(240);

// After reset, we poll the magic number register for this long.
// Observed time is 1000ms.
static constexpr base::TimeDelta kMagicSleep = base::Milliseconds(100);
static constexpr base::TimeDelta kMagicTimeout = base::Milliseconds(3000);

// After requesting application launch, we must wait for verification
// Observed time is 100 seconds.
static constexpr base::TimeDelta kApplTimeout = base::Milliseconds(200000);
static constexpr base::TimeDelta kApplSleep = base::Milliseconds(1000);

// Time from powering on the sensor to it becoming ready for communication.
static constexpr base::TimeDelta kPowerOnDelay = base::Milliseconds(1000);

// Time for letting the sensor settle after powering it off.
static constexpr base::TimeDelta kPowerOffDelay = base::Milliseconds(100);

// Special exit code to prevent upstart respawning us and crash
// service-failure-hpsd from being uploaded. See normal exit.
static constexpr int kNoRespawnExit = 5;

// Initialise the firmware parameters.
void HPS_impl::Init(uint32_t stage1_version,
                    const base::FilePath& mcu,
                    const base::FilePath& fpga_bitstream,
                    const base::FilePath& fpga_app_image) {
  this->required_stage1_version_ = stage1_version;
  this->mcu_blob_ = mcu;
  this->fpga_bitstream_ = fpga_bitstream;
  this->fpga_app_image_ = fpga_app_image;
}

// Attempt the boot sequence
// returns true if booting completed
void HPS_impl::Boot() {
  // Make sure blobs are set etc.
  if (this->mcu_blob_.empty() || this->fpga_bitstream_.empty() ||
      this->fpga_app_image_.empty()) {
    LOG(FATAL) << "No HPS firmware to download.";
  }

  this->Reboot();

  this->boot_start_time_ = base::TimeTicks::Now();
  // If the boot process sent an update, reboot and try again
  // A full update takes 3 boots, so try 3 times.
  for (int i = 0; i < 3; ++i) {
    switch (this->TryBoot()) {
      case BootResult::kOk:
        LOG(INFO) << "HPS device booted";
        return;
      case BootResult::kUpdate:
        LOG(INFO) << "Update sent, rebooting";
        this->Reboot();
        continue;
    }
  }
  OnFatalError(FROM_HERE, "Boot failure, too many updates.");
}

bool HPS_impl::Enable(uint8_t feature) {
  DCHECK(wake_lock_);
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
  DCHECK(wake_lock_);
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
  DCHECK(wake_lock_);
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
  hps_metrics_->SendImageValidity(result.valid);

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
        OnFatalError(FROM_HERE, "Launch stage 1 failed");
      }
      break;
    case BootResult::kUpdate:
      if (mcu_update_sent_) {
        LOG(ERROR) << "Failed to boot after MCU update, giving up";
        hps_metrics_->SendHpsTurnOnResult(
            HpsTurnOnResult::kMcuUpdatedThenFailed,
            base::TimeTicks::Now() - this->boot_start_time_);
        exit(kNoRespawnExit);
      }
      mcu_update_sent_ = true;
      SendStage1Update();
      return BootResult::kUpdate;
  }

  // Inspect stage1 flags and either fail or launch application and continue
  this->CheckStage1();
  VLOG(1) << "Launching Application";
  if (!this->device_->WriteReg(HpsReg::kSysCmd, R3::kLaunchAppl)) {
    OnFatalError(FROM_HERE, "Launch Application failed");
  }

  // Inspect application flags and either fail, send an update, or succeed
  switch (this->CheckApplication()) {
    case BootResult::kOk:
      VLOG(1) << "Application Running";
      return BootResult::kOk;
    case BootResult::kUpdate:
      if (spi_update_sent_) {
        LOG(ERROR) << "Failed to boot after SPI update, giving up";
        hps_metrics_->SendHpsTurnOnResult(
            HpsTurnOnResult::kSpiUpdatedThenFailed,
            base::TimeTicks::Now() - this->boot_start_time_);
        exit(kNoRespawnExit);
      }
      spi_update_sent_ = true;
      SendApplicationUpdate();
      return BootResult::kUpdate;
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
        return false;
      }
    } else if (magic == kHpsMagic) {
      VLOG(1) << "Good magic number after " << timer.Elapsed().InMilliseconds()
              << "ms";
      return true;
    } else {
      hps_metrics_->SendHpsTurnOnResult(
          HpsTurnOnResult::kBadMagic,
          base::TimeTicks::Now() - this->boot_start_time_);
      OnFatalError(FROM_HERE, base::StringPrintf("Bad magic number 0x%04x",
                                                 magic.value()));
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
hps::HPS_impl::BootResult HPS_impl::CheckStage0() {
  if (!CheckMagic()) {
    hps_metrics_->SendHpsTurnOnResult(
        HpsTurnOnResult::kNoResponse,
        base::TimeTicks::Now() - this->boot_start_time_);
    OnFatalError(FROM_HERE, "Timeout waiting for stage0 magic number");
  }

  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status) {
    // TODO(evanbenn) log a metric
    OnFatalError(FROM_HERE, "ReadReg failure");
  }

  if (status.value() & R2::kFault || !(status.value() & R2::kOK)) {
    OnBootFault(FROM_HERE);
  }

  std::optional<uint16_t> hwrev = this->device_->ReadReg(HpsReg::kHwRev);
  if (!hwrev) {
    // TODO(evanbenn) log a metric
    OnFatalError(FROM_HERE, "Failed to read hwrev");
  }
  this->hw_rev_ = hwrev.value();

  this->write_protect_off_ = status.value() & R2::kWpOff;
  VLOG_IF(1, this->write_protect_off_) << "kWpOff, ignoring verified bits";

  // When write protect is off we ignore the verified signal.
  // When write protect is not off we update if there is no verified signal.
  if (!this->write_protect_off_ && !(status.value() & R2::kStage1Verified)) {
    // Stage1 not verified, so need to update it.
    LOG(INFO) << "Stage1 flash not verified";
    hps_metrics_->SendHpsTurnOnResult(
        HpsTurnOnResult::kMcuNotVerified,
        base::TimeTicks::Now() - this->boot_start_time_);
    return BootResult::kUpdate;
  }

  // Verified, so now check the version. If it is different, update it.
  std::optional<uint16_t> version_low =
      this->device_->ReadReg(HpsReg::kFirmwareVersionLow);
  std::optional<uint16_t> version_high =
      this->device_->ReadReg(HpsReg::kFirmwareVersionHigh);
  if (!version_low || !version_high) {
    // TODO(evanbenn) log a metric
    OnFatalError(FROM_HERE, "ReadReg failure");
  }
  this->actual_stage1_version_ =
      static_cast<uint32_t>(version_high.value() << 16) | version_low.value();
  if (this->actual_stage1_version_ == this->required_stage1_version_) {
    // Stage 1 is verified
    VLOG(1) << "Stage1 version OK";
    return BootResult::kOk;
  } else {
    // Versions do not match, need to update.
    LOG(INFO) << "Stage1 version mismatch, module: "
              << this->actual_stage1_version_
              << " expected: " << this->required_stage1_version_;
    hps_metrics_->SendHpsTurnOnResult(
        HpsTurnOnResult::kMcuVersionMismatch,
        base::TimeTicks::Now() - this->boot_start_time_);
    return BootResult::kUpdate;
  }
}

// Check stage1 status:
// Check status flags.
// Check spi verification.
// Returns if booting should continue.
void HPS_impl::CheckStage1() {
  if (!CheckMagic()) {
    hps_metrics_->SendHpsTurnOnResult(
        HpsTurnOnResult::kStage1NotStarted,
        base::TimeTicks::Now() - this->boot_start_time_);
    OnFatalError(FROM_HERE, "Timeout waiting for stage1 magic number");
  }

  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status) {
    // TODO(evanbenn) log a metric
    OnFatalError(FROM_HERE, "ReadReg failure");
  }

  if (status.value() & R2::kFault || !(status.value() & R2::kOK)) {
    OnBootFault(FROM_HERE);
  }

  if (!(status.value() & R2::kStage1)) {
    hps_metrics_->SendHpsTurnOnResult(
        HpsTurnOnResult::kStage1NotStarted,
        base::TimeTicks::Now() - this->boot_start_time_);
    OnFatalError(FROM_HERE, "Stage 1 did not start");
  }
  VLOG(1) << "Stage 1 OK";
}

// Check stage2 status:
// Check status flags.
// Return BootResult::kOk if application is running.
// Return BootResult::kUpdate if an update should be sent.
hps::HPS_impl::BootResult HPS_impl::CheckApplication() {
  // Poll for kAppl (started) or kSpiNotVer (not started)
  base::ElapsedTimer timer;
  do {
    std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
    if (!status) {
      // TODO(evanbenn) log a metric
      OnFatalError(FROM_HERE, "ReadReg failure");
    }
    if (status.value() & R2::kAppl) {
      VLOG(1) << "Application boot after " << timer.Elapsed().InMilliseconds()
              << "ms";
      hps_metrics_->SendHpsTurnOnResult(
          HpsTurnOnResult::kSuccess,
          base::TimeTicks::Now() - this->boot_start_time_);
      return BootResult::kOk;
    }

    std::optional<uint16_t> error = this->device_->ReadReg(HpsReg::kError);
    if (!error) {
      // TODO(evanbenn) log a metric
      OnFatalError(FROM_HERE, "ReadReg failure");
    }
    if (error.value() == RError::kSpiFlashNotVerified) {
      VLOG(1) << "SPI verification failed after "
              << timer.Elapsed().InMilliseconds() << "ms";
      hps_metrics_->SendHpsTurnOnResult(
          HpsTurnOnResult::kSpiNotVerified,
          base::TimeTicks::Now() - this->boot_start_time_);
      return BootResult::kUpdate;
    } else if (error.value()) {
      OnBootFault(FROM_HERE);
    }

    Sleep(kApplSleep);
  } while (timer.Elapsed() < kApplTimeout);

  hps_metrics_->SendHpsTurnOnResult(
      HpsTurnOnResult::kApplNotStarted,
      base::TimeTicks::Now() - this->boot_start_time_);
  OnFatalError(FROM_HERE, "Application did not start");
}

// Reboot the hardware module.
bool HPS_impl::Reboot() {
  if (wake_lock_)
    ShutDown();
  LOG(INFO) << "Starting HPS device";
  wake_lock_ = device_->CreateWakeLock();
  Sleep(kPowerOnDelay);

  // Also send a reset cmd in case the kernel driver isn't present.
  if (!this->device_->WriteReg(HpsReg::kSysCmd, R3::kReset)) {
    OnFatalError(FROM_HERE, "Reboot failed");
  }
  return true;
}

bool HPS_impl::ShutDown() {
  DCHECK(wake_lock_);
  LOG(INFO) << "Shutting down HPS device";
  wake_lock_.reset();
  feat_enabled_ = 0;
  Sleep(kPowerOffDelay);
  return true;
}

bool HPS_impl::IsRunning() {
  DCHECK(wake_lock_);
  // Check the application is enabled and running.
  std::optional<uint16_t> status = this->device_->ReadReg(HpsReg::kSysStatus);
  if (!status || !(status.value() & R2::kAppl)) {
    LOG(ERROR) << "Fault: application not running";
    return false;
  }

  // Check for errors.
  std::optional<uint16_t> errors = this->device_->ReadReg(HpsReg::kError);
  if (errors.has_value() && errors.value()) {
    std::string msg =
        "Error " + HpsRegValToString(HpsReg::kError, errors.value());
    OnFatalError(FROM_HERE, msg);
  }
  return true;
}

// Fault bit seen during boot, attempt to dump status information and abort.
// Only call this function in the boot process.
[[noreturn]] void HPS_impl::OnBootFault(const base::Location& location) {
  hps_metrics_->SendHpsTurnOnResult(
      HpsTurnOnResult::kFault, base::TimeTicks::Now() - this->boot_start_time_);
  OnFatalError(location, "Boot fault");
}

[[noreturn]] void HPS_impl::OnFatalError(const base::Location& location,
                                         const std::string& msg) {
  LOG(ERROR) << "Fatal error at " << location.ToString() << ": " << msg;
  LOG(ERROR) << base::StringPrintf("- Requested feature status: 0x%04x",
                                   feat_enabled_);
  LOG(ERROR) << base::StringPrintf("- Stage1 rootfs version: 0x%08x",
                                   required_stage1_version_);
  LOG(ERROR) << base::StringPrintf("- Stage1 running version: 0x%08x",
                                   actual_stage1_version_);
  LOG(ERROR) << base::StringPrintf("- HW rev: 0x%04x", hw_rev_);
  LOG(ERROR) << base::StringPrintf("- Updates sent: mcu:%d spi:%d",
                                   mcu_update_sent_, spi_update_sent_);
  LOG(ERROR) << base::StringPrintf("- Wake lock: %d", !!wake_lock_);
  DumpHpsRegisters(*device_,
                   [](const std::string& s) { LOG(ERROR) << "- " << s; });
  LOG(FATAL) << "Terminating for fatal error at " << location.ToString() << ": "
             << msg;
  abort();
}

// Send the stage1 MCU flash update.
// Returns if update was sent
void HPS_impl::SendStage1Update() {
  LOG(INFO) << "Updating MCU flash";
  base::ElapsedTimer timer;
  if (this->Download(HpsBank::kMcuFlash, this->mcu_blob_)) {
    hps_metrics_->SendHpsUpdateDuration(HpsBank::kMcuFlash, timer.Elapsed());
  } else {
    hps_metrics_->SendHpsTurnOnResult(
        HpsTurnOnResult::kMcuUpdateFailure,
        base::TimeTicks::Now() - this->boot_start_time_);
    OnFatalError(FROM_HERE, "Failed sending stage1 update");
  }
}

// Send the Application SPI flash update.
// Returns kFail or kUpdate.
void HPS_impl::SendApplicationUpdate() {
  LOG(INFO) << "Updating SPI flash";
  base::ElapsedTimer timer;
  if (this->Download(HpsBank::kSpiFlash, this->fpga_bitstream_) &&
      this->Download(HpsBank::kSocRom, this->fpga_app_image_)) {
    hps_metrics_->SendHpsUpdateDuration(HpsBank::kSpiFlash, timer.Elapsed());
  } else {
    hps_metrics_->SendHpsTurnOnResult(
        HpsTurnOnResult::kSpiUpdateFailure,
        base::TimeTicks::Now() - this->boot_start_time_);
    OnFatalError(FROM_HERE, "Failed sending stage1 update");
  }
}

/*
 * Download data to the bank specified.
 * The HPS/Host I2C Interface Memory Write is used.
 */
bool HPS_impl::Download(hps::HpsBank bank, const base::FilePath& source) {
  DCHECK(wake_lock_);
  uint8_t ibank = static_cast<uint8_t>(bank);
  if (ibank >= kNumBanks) {
    LOG(ERROR) << "Download: Illegal bank: " << static_cast<int>(ibank) << ": "
               << source;
    return -1;
  }
  std::optional<std::vector<uint8_t>> contents = this->DecompressFile(source);
  if (!contents.has_value())
    return false;
  return this->WriteFile(ibank, source, contents.value());
}

std::optional<std::vector<uint8_t>> HPS_impl::DecompressFile(
    const base::FilePath& source) {
  std::string compressed_contents;
  if (!base::ReadFileToString(source, &compressed_contents)) {
    PLOG(ERROR) << "DecompressFile: \"" << source << "\": Reading failed";
    return std::nullopt;
  }

  if (source.FinalExtension() != ".xz") {
    // Assume it's not actually compressed and return its contents as is.
    std::vector<uint8_t> uncompressed(compressed_contents.begin(),
                                      compressed_contents.end());
    return std::make_optional(std::move(uncompressed));
  }

  std::vector<uint8_t> decompressed(2 * 1024 * 1024);  // max 2MB decompressed
  uint64_t memlimit = 20 * 1024 * 1024;  // limit decoder to allocating 20MB
  size_t in_pos = 0;
  size_t out_pos = 0;
  lzma_ret ret = lzma_stream_buffer_decode(
      &memlimit, /* flags */ 0, /* allocator */ nullptr,
      reinterpret_cast<const uint8_t*>(compressed_contents.data()), &in_pos,
      compressed_contents.size(), decompressed.data(), &out_pos,
      decompressed.size());
  if (ret != LZMA_OK) {
    LOG(ERROR) << "DecompressFile: \"" << source
               << "\": Decompressing failed with error " << ret;
    return std::nullopt;
  }
  decompressed.resize(out_pos);
  return std::make_optional(std::move(decompressed));
}

void HPS_impl::SetDownloadObserver(DownloadObserver observer) {
  this->download_observer_ = std::move(observer);
}

/*
 * Write the file to the bank indicated.
 */
bool HPS_impl::WriteFile(uint8_t bank,
                         const base::FilePath& source,
                         const std::vector<uint8_t>& contents) {
  switch (bank) {
    case static_cast<uint8_t>(HpsBank::kMcuFlash):
      if (!this->device_->WriteReg(HpsReg::kSysCmd, R3::kEraseStage1)) {
        LOG(ERROR) << "WriteFile: error erasing bank: "
                   << static_cast<int>(bank);
        return false;
      }
      break;
    case static_cast<uint8_t>(HpsBank::kSpiFlash):
      // Note that this also erases bank 2 (HpsBank::kSocRom)
      // because they are both on the same SPI flash!
      if (!this->device_->WriteReg(HpsReg::kSysCmd, R3::kEraseSpiFlash)) {
        LOG(ERROR) << "WriteFile: error erasing bank: "
                   << static_cast<int>(bank);
        return false;
      }
      break;
    case static_cast<uint8_t>(HpsBank::kSocRom):
      // Assume it was already erased by writing HpsBank::kSpiFlash before this.
      break;
  }
  if (!this->WaitForBankReady(bank)) {
    LOG(ERROR) << "WriteFile: bank " << static_cast<int>(bank)
               << " not ready after erase";
    return false;
  }
  base::ElapsedTimer timer;
  size_t block_size = this->device_->BlockSizeBytes();
  /*
   * Leave room for a 32 bit address at the start of the block to be written.
   * The address is updated for each block to indicate
   * where this block is to be written.
   * The format of the data block is:
   *    4 bytes of address in big endian format
   *    data
   */
  auto buf = std::make_unique<uint8_t[]>(block_size + sizeof(uint32_t));
  // Iterate over the firmware contents in blocks of *block_size* bytes.
  auto block_begin = contents.begin();
  while (block_begin != contents.end()) {
    // The current block ends after *block_size* bytes,
    // or at end of *contents* if there are fewer bytes remaining.
    auto block_end = std::distance(block_begin, contents.end()) >=
                             static_cast<std::ptrdiff_t>(block_size)
                         ? block_begin + block_size
                         : contents.end();
    // The address is just the offset of the current block from the beginning.
    uint32_t address =
        static_cast<uint32_t>(std::distance(contents.begin(), block_begin));
    buf[0] = address >> 24;
    buf[1] = (address >> 16) & 0xff;
    buf[2] = (address >> 8) & 0xff;
    buf[3] = address & 0xff;
    std::copy(block_begin, block_end, &buf[sizeof(uint32_t)]);
    size_t length = std::distance(block_begin, block_end) + sizeof(uint32_t);
    if (!this->device_->Write(I2cMemWrite(bank), &buf[0], length)) {
      LOG(ERROR) << "WriteFile: device write error. bank: "
                 << static_cast<int>(bank);
      return false;
    }
    // Wait for the bank to become ready, indicating that the previous write has
    // finished.
    if (!this->WaitForBankReady(bank)) {
      LOG(ERROR) << "WriteFile: bank " << static_cast<int>(bank)
                 << " not ready after write";
      return false;
    }
    if (download_observer_) {
      download_observer_.Run(source, static_cast<uint32_t>(contents.size()),
                             std::distance(contents.begin(), block_end),
                             timer.Elapsed());
    }
    block_begin = block_end;
  }
  VLOG(1) << "Wrote " << contents.size() << " bytes from " << source << " in "
          << timer.Elapsed().InMilliseconds() << "ms";
  return true;
}

bool HPS_impl::WaitForBankReady(uint8_t bank) {
  base::ElapsedTimer timer;
  do {
    std::optional<uint16_t> result = this->device_->ReadReg(HpsReg::kBankReady);
    if (result && (result.value() & (1 << bank))) {
      return true;
    }
    Sleep(kBankReadySleep);
  } while (timer.Elapsed() < kBankReadyTimeout);
  return false;
}

}  // namespace hps
