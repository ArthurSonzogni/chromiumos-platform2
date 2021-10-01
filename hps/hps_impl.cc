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

#include "hps/hps_impl.h"
#include "hps/hps_reg.h"

namespace hps {

static const int kTimeoutMs = 250;  // Bank ready timeout.
static const int kPollMs = 5;       // Delay time for poll.
static const int kMaxBootRetries = 5;

// Initialise the firmware parameters.
void HPS_impl::Init(uint32_t appl_version,
                    const base::FilePath& mcu,
                    const base::FilePath& spi) {
  this->appl_version_ = appl_version;
  this->mcu_blob_ = mcu;
  this->spi_blob_ = spi;
}

void HPS_impl::SkipBoot() {
  // Force state to ready.
  LOG(INFO) << "Forcing module state to ready";
  this->Go(State::kReady);
}

bool HPS_impl::Boot() {
  // Exclusive access to module.
  base::AutoLock l(this->lock_);
  // Make sure blobs are set etc.
  if (this->mcu_blob_.empty() || this->spi_blob_.empty()) {
    LOG(ERROR) << "No HPS firmware to download.";
    return false;
  }
  this->reboots_ = 0;
  this->Go(State::kBoot);
  // Run the boot state machine it terminates with the
  // module either ready, or in a failed state.
  for (;;) {
    this->HandleState();
    if (this->state_ == State::kFailed) {
      return false;
    }
    if (this->state_ == State::kReady) {
      return true;
    }
    // Short delay between running the states.
    base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
  }
}

bool HPS_impl::Enable(uint8_t feature) {
  // Exclusive access to module.
  base::AutoLock l(this->lock_);
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
  // Exclusive access to module.
  base::AutoLock l(this->lock_);
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
  // Exclusive access to module.
  base::AutoLock l(this->lock_);
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
      hps_result = this->device_->ReadReg(HpsReg::kF1);
      break;
    case 1:
      hps_result = this->device_->ReadReg(HpsReg::kF2);
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
  result.inference_result = hps_result & 0xFF;
  return result;
}

// Runs the state machine.
void HPS_impl::HandleState() {
  switch (this->state_) {
    case kBoot: {
      // Wait for magic number.
      int magic = this->device_->ReadReg(HpsReg::kMagic);
      if (magic == kHpsMagic) {
        this->Go(State::kBootCheckFault);
      } else if (magic != -1) {
        LOG(INFO) << "Bad magic number: " << magic;
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kBadMagic);
        this->Go(State::kFailed);
      } else if (this->retries_++ >= 50) {
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kNoResponse);
        this->Reboot("Timeout waiting for boot magic number");
      }
      break;
    }

    case kBootCheckFault: {
      // Wait for OK or Fault.
      if (this->retries_++ >= 50) {
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kTimeout);
        this->Reboot("Timeout waiting for boot OK/Fault");
        break;
      }
      {
        int status = this->device_->ReadReg(HpsReg::kSysStatus);
        if (status >= 0) {
          if (status & R2::kFault) {
            hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kFault);
            this->Fault();
          } else if (status & R2::kOK) {
            // Module has reported OK.
            // Store h/w version.
            int hwrev = this->device_->ReadReg(HpsReg::kHwRev);
            if (hwrev >= 0) {
              this->hw_rev_ = hwrev;
              this->Go(State::kBootOK);
            } else {
              LOG(INFO) << "Failed to read hwrev";
            }
          }
        }
      }
      break;
    }

    case kBootOK: {
      if (this->retries_++ >= 50) {
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kTimeout);
        this->Reboot("Timeout boot appl verification");
        break;
      }
      // Wait for application verified or not.
      int status = this->device_->ReadReg(HpsReg::kSysStatus);
      if (status >= 0) {
        // Check if the MCU flash verified or not.
        if (status & R2::kApplNotVerified) {
          // Appl not verified, so need to update it.
          LOG(INFO) << "Appl flash not verified, updating";
          hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kMcuNotVerified);
          this->Go(State::kUpdateAppl);
        } else if (status & R2::kApplVerified) {
          // Verified, so now check the version. If it is
          // different, update it.
          int version_low = this->device_->ReadReg(HpsReg::kFirmwareVersionLow);
          int version_high =
              this->device_->ReadReg(HpsReg::kFirmwareVersionHigh);
          if (version_low >= 0 && version_high >= 0) {
            uint32_t version = (static_cast<uint16_t>(version_high) << 16) |
                               static_cast<uint16_t>(version_low);
            if (version == this->appl_version_) {
              // Application is verified, launch it.
              VLOG(1) << "Launching to stage1";
              this->device_->WriteReg(HpsReg::kSysCmd, R3::kLaunch);
              this->Go(State::kStage1);
            } else {
              // Versions do not match, need to update.
              LOG(INFO) << "Appl version mismatch, module: " << version
                        << " expected: " << this->appl_version_;
              hps_metrics_.SendHpsTurnOnResult(
                  HpsTurnOnResult::kMcuVersionMismatch);
              this->Go(State::kUpdateAppl);
            }
          }
        }
      }
      break;
    }

    case kUpdateAppl: {
      // Update the MCU flash.
      if (this->WriteFile(0, this->mcu_blob_)) {
        this->Reboot("MCU flash updated");
      } else if (this->retries_++ > 5) {
        this->Reboot("MCU flash failed to update");
      }
      break;
    }

    case kUpdateSpi: {
      // Update the SPI flash.
      if (this->WriteFile(1, this->spi_blob_)) {
        this->Reboot("SPI flash updated");
      } else if (this->retries_++ > 5) {
        this->Reboot("SPI flash failed to update");
      }
      break;
    }

    case kStage1: {
      // Wait for stage1 bit.
      if ((this->device_->ReadReg(HpsReg::kMagic) == kHpsMagic) &&
          (this->device_->ReadReg(HpsReg::kSysStatus) & R2::kStage1)) {
        this->Go(State::kSpiVerify);
      } else if (this->retries_++ >= 50) {
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kStage1NotStarted);
        this->Reboot("Timeout stage1");
      }
      break;
    }

    case kSpiVerify: {
      // Wait for SPI verified or not.
      if (this->retries_++ >= 50) {
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kTimeout);
        this->Reboot("Timeout SPI verify");
      }
      {
        int status = this->device_->ReadReg(HpsReg::kSysStatus);
        if (status >= 0) {
          // Check if the SPI flash verified or not.
          if (status & R2::kSpiNotVerified) {
            // Spi not verified, so need to update it.
            LOG(INFO) << "SPI flash not verified, updating";
            hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kSpiNotVerified);
            this->Go(State::kUpdateSpi);
          } else if (status & R2::kSpiVerified) {
            VLOG(1) << "Enabling application";
            this->device_->WriteReg(HpsReg::kSysCmd, R3::kEnable);
            this->Go(State::kApplWait);
          }
        }
      }
      break;
    }

    case kApplWait: {
      // Wait for application running bit.
      int status = this->device_->ReadReg(HpsReg::kSysStatus);
      if (this->device_->ReadReg(HpsReg::kMagic) == kHpsMagic && status >= 0 &&
          status & R2::kAppl) {
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kSuccess);
        this->Go(State::kReady);
      } else if (this->retries_++ >= 50) {
        hps_metrics_.SendHpsTurnOnResult(HpsTurnOnResult::kApplNotStarted);
        this->Reboot("Timeout application");
      }
    } break;

    case kReady: {
      // The state machine is not run in this state
      NOTREACHED();
      break;
    }

    case kFailed: {
      // The state machine is not run in this state
      NOTREACHED();
      break;
    }
  }
}

// Reboot the hardware module.
void HPS_impl::Reboot(const char* msg) {
  if (++this->reboots_ > kMaxBootRetries) {
    LOG(ERROR) << "Too many reboots, giving up";
    this->Go(State::kFailed);
  } else {
    LOG(INFO) << "Rebooting: " << msg;
    // Send a reset cmd - maybe should power cycle.
    this->device_->WriteReg(HpsReg::kSysCmd, R3::kReset);
    this->Go(State::kBoot);
  }
}

// Fault bit seen, attempt to dump status information, and
// try to reboot the module.
// If the count of reboots is too high, set the module as failed.
void HPS_impl::Fault() {
  int errors = this->device_->ReadReg(HpsReg::kError);
  if (errors < 0) {
    this->Reboot("Fault: cause unknown");
  } else {
    this->Reboot(
        base::StringPrintf("Fault: cause 0x%04x", static_cast<unsigned>(errors))
            .c_str());
  }
}

// Move to new state, reset retry counter.
void HPS_impl::Go(State newstate) {
  VLOG(1) << "Old state: " << this->state_ << " new state: " << newstate;
  this->state_ = newstate;
  this->retries_ = 0;
}

/*
 * Download data to the bank specified.
 * The HPS/Host I2C Interface Memory Write is used.
 */
bool HPS_impl::Download(hps::HpsBank bank, const base::FilePath& source) {
  // Exclusive access to module.
  base::AutoLock l(this->lock_);
  int ibank = static_cast<int>(bank);
  if (ibank < 0 || ibank >= kNumBanks) {
    LOG(ERROR) << "Download: Illegal bank: " << ibank << ": " << source;
    return -1;
  }
  return this->WriteFile(ibank, source);
}

/*
 * Write the file to the bank indicated.
 */
bool HPS_impl::WriteFile(int bank, const base::FilePath& source) {
  base::File file(source,
                  base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  if (!file.IsValid()) {
    LOG(ERROR) << "WriteFile: " << source << ": "
               << base::File::ErrorToString(file.error_details());
    return false;
  }
  int bytes = 0;
  uint32_t address = 0;
  int rd;
  do {
    if (!this->WaitForBankReady(bank)) {
      LOG(ERROR) << "WriteFile: bank not ready: " << bank;
      return false;
    }
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
    buf[0] = address >> 24;
    buf[1] = address >> 16;
    buf[2] = address >> 8;
    buf[3] = address;
    rd = file.ReadAtCurrentPos(reinterpret_cast<char*>(&buf[sizeof(uint32_t)]),
                               this->device_->BlockSizeBytes());
    if (rd > 0) {
      if (!this->device_->Write(I2cMemWrite(bank), &buf[0],
                                rd + sizeof(uint32_t))) {
        LOG(ERROR) << "WriteFile: device write error. bank: " << bank;
        return false;
      }
      address += rd;
      bytes += rd;
    }
  } while (rd > 0);  // A read returning 0 indicates EOF.
  VLOG(1) << "Wrote " << bytes << " bytes from " << source;
  // Wait for the bank to become ready again to ensure the write is complete.
  this->WaitForBankReady(bank);
  return true;
}

bool HPS_impl::WaitForBankReady(int bank) {
  int tout = 0;
  for (;;) {
    int result = this->device_->ReadReg(HpsReg::kBankReady);
    if (result < 0) {
      return false;
    }
    if (result & (1 << bank)) {
      break;
    }
    // If timed out, finish the write.
    if (tout >= kTimeoutMs) {
      return false;
    }
    base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(kPollMs));
    tout += kPollMs;
  }
  return true;
}

}  // namespace hps
