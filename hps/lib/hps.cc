// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Main HPS class.
 */

#include <fstream>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread.h>
#include <base/time/time.h>

#include "hps/lib/hps.h"
#include "hps/lib/hps_reg.h"

namespace hps {

static const int kBlock = 256;
static const int kTimeoutMs = 250;  // Bank ready timeout
static const int kPollMs = 5;       // Delay time for poll.
static const int kMaxBootRetries = 5;

// Initialise the firmware parameters.
void HPS::Init(uint16_t appl_version,
               const base::FilePath& mcu,
               const base::FilePath& spi) {
  this->appl_version_ = appl_version;
  this->mcu_blob_ = mcu;
  this->spi_blob_ = spi;
}

bool HPS::Boot() {
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

bool HPS::Enable(uint16_t features) {
  // Only 2 features available at the moment.
  this->feat_enabled_ = features & 0x3;
  // Check the application is enabled and running.
  if ((this->device_->ReadReg(HpsReg::kSysStatus) & R2::kAppl) == 0) {
    return false;
  }
  // Write the enable feature mask.
  return this->device_->WriteReg(HpsReg::kFeatEn, this->feat_enabled_);
}

int HPS::Result(int feature) {
  // Check the application is enabled and running.
  if ((this->device_->ReadReg(HpsReg::kSysStatus) & R2::kAppl) == 0) {
    return -1;
  }
  // Check that feature is enabled.
  if (((1 << feature) & this->feat_enabled_) == 0) {
    return -1;
  }
  int result;
  switch (feature) {
    case 0:
      result = this->device_->ReadReg(HpsReg::kF1);
      break;
    case 1:
      result = this->device_->ReadReg(HpsReg::kF2);
      break;
    default:
      result = -1;
  }
  if (result < 0) {
    return -1;
  }
  // Check that valid bit is on.
  if ((result & RFeat::kValid) == 0) {
    return -1;
  }
  // Return lower 15 bits.
  return result & 0x7FFF;
}

// Runs the state machine.
void HPS::HandleState() {
  switch (this->state_) {
    case kBoot:
      // Wait for magic number.
      if (this->device_->ReadReg(HpsReg::kMagic) == kHpsMagic) {
        this->Go(State::kBootCheckFault);
      } else if (this->retries_++ >= 50) {
        this->Fail("Timeout waiting for boot magic number");
      }
      break;

    case kBootCheckFault:
      // Wait for OK or Fault.
      if (this->retries_++ >= 50) {
        this->Fail("Timeout waiting for boot OK/Fault");
        break;
      }
      {
        uint16_t status = this->device_->ReadReg(HpsReg::kSysStatus);
        if (status >= 0) {
          if (status & R2::kFault) {
            this->Fault();
          } else if (status & R2::kOK) {
            // Module has reported OK.
            // Store h/w version.
            this->hw_rev_ = this->device_->ReadReg(HpsReg::kHwRev);
            this->Go(State::kBootOK);
          }
        }
      }
      break;

    case kBootOK:
      if (this->retries_++ >= 50) {
        this->Fail("Timeout boot appl verification");
        break;
      }
      // Wait for application verified or not.
      {
        uint16_t r = this->device_->ReadReg(HpsReg::kSysStatus);
        if (r >= 0) {
          // Check if the MCU flash verified or not.
          if (r & R2::kApplNotVerified) {
            // Appl not verified, so need to update it.
            LOG(INFO) << "Appl flash not verified, updating";
            this->Go(State::kUpdateAppl);
          } else if (r & R2::kApplVerified) {
            // Verified, so now check the version. If it is
            // different, update it.
            r = this->device_->ReadReg(HpsReg::kApplVers);
            if (r == this->appl_version_) {
              // Application is verified, launch it.
              VLOG(1) << "Launching to stage1";
              this->device_->WriteReg(HpsReg::kSysCmd, R3::kLaunch);
              this->Go(State::kStage1);
            } else {
              // Versions do not match, need to update.
              LOG(INFO) << "Appl version mismatch, updating";
              this->Go(State::kUpdateAppl);
            }
          }
        }
      }
      break;

    case kUpdateAppl:
      // Update the MCU flash.
      if (this->Download(0, this->mcu_blob_)) {
        this->Reboot("MCU flash updated");
      } else if (this->retries_ > 5) {
        this->Fail("MCU flash");
      }
      break;

    case kUpdateSpi:
      // Update the SPI flash.
      if (this->Download(1, this->spi_blob_)) {
        this->Reboot("SPI flash updated");
      } else if (this->retries_ > 5) {
        this->Fail("SPI flash");
      }
      break;

    case kStage1:
      // Wait for stage1 bit.
      if ((this->device_->ReadReg(HpsReg::kMagic) == kHpsMagic) &&
          (this->device_->ReadReg(HpsReg::kSysStatus) & R2::kStage1)) {
        this->Go(State::kSpiVerify);
      } else if (this->retries_++ >= 50) {
        this->Fail("Timeout stage1");
      }
      break;

    case kSpiVerify:
      // Wait for SPI verified or not.
      if (this->retries_++ >= 50) {
        this->Fail("Timeout SPI verify");
      }
      {
        uint16_t r = this->device_->ReadReg(HpsReg::kSysStatus);
        if (r >= 0) {
          // Check if the SPI flash verified or not.
          if (r & R2::kSpiNotVerified) {
            // Spi not verified, so need to update it.
            LOG(INFO) << "SPI flash not verified, updating";
            this->Go(State::kUpdateSpi);
          } else if (r & R2::kSpiVerified) {
            VLOG(1) << "Enabling application";
            this->device_->WriteReg(HpsReg::kSysCmd, R3::kEnable);
            this->Go(State::kApplWait);
          }
        }
      }
      break;

    case kApplWait:
      // Wait for application running bit.
      if ((this->device_->ReadReg(HpsReg::kMagic) == kHpsMagic) &&
          (this->device_->ReadReg(HpsReg::kSysStatus) & R2::kAppl)) {
        this->Go(State::kReady);
      } else if (this->retries_++ >= 50) {
        this->Fail("Timeout application");
      }
      break;

    case kReady:
      break;

    case kFailed:
      // Nothing to do. Wait until module re-initialised.
      break;
  }
}

// Something went wrong, so reboot to try again.
void HPS::Fail(const char* msg) {
  if (++this->reboots_ > kMaxBootRetries) {
    LOG(ERROR) << "Too many reboots, giving up";
    this->Go(State::kFailed);
  } else {
    this->Reboot(msg);
  }
}

// Reboot the module.
void HPS::Reboot(const char* msg) {
  LOG(INFO) << "Rebooting: " << msg;
  // Send a reset cmd - maybe should power cycle.
  this->device_->WriteReg(HpsReg::kSysCmd, R3::kReset);
  this->Go(State::kBoot);
}

// Fault bit seen, attempt to dump status information, and
// try to reboot the module.
// If the count of reboots is too high, set the module as failed.
void HPS::Fault() {
  int errors = this->device_->ReadReg(HpsReg::kError);
  this->Fail(base::StringPrintf("Fault: cause 0x%04x", errors).c_str());
}

// Move to new state, reset retry counter.
void HPS::Go(State newstate) {
  VLOG(1) << "Old state: " << this->state_ << " new state: " << newstate;
  this->state_ = newstate;
  this->retries_ = 0;
}

/*
 * Download data to the bank specified.
 * The HPS/Host I2C Interface Memory Write is used.
 */
bool HPS::Download(int bank, const base::FilePath& source) {
  if (bank < 0 || bank >= kNumBanks) {
    return -1;
  }
  base::File file(source,
                  base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  if (!file.IsValid()) {
    LOG(ERROR) << "Download: " << source << ": " << file.error_details();
    return false;
  }
  int bytes = 0;
  uint32_t address = 0;
  int rd;
  do {
    if (!this->WaitForBankReady(bank)) {
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
    uint8_t buf[kBlock + sizeof(uint32_t)];
    buf[0] = address >> 24;
    buf[1] = address >> 16;
    buf[2] = address >> 8;
    buf[3] = address;
    rd = file.ReadAtCurrentPos(reinterpret_cast<char*>(&buf[sizeof(uint32_t)]),
                               kBlock);
    if (rd > 0) {
      if (!this->device_->Write(I2cMemWrite(bank), buf,
                                rd + sizeof(uint32_t))) {
        return false;
      }
      address += rd;
      bytes += rd;
    }
  } while (rd > 0);  // A read returning 0 indicates EOF.
  VLOG(1) << "Downloaded " << bytes << " bytes from " << source;
  // Wait for the bank to become ready again to ensure the
  // write has completed.
  this->WaitForBankReady(bank);
  return true;
}

bool HPS::WaitForBankReady(int bank) {
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
