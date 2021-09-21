// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Simulated HPS hardware device.
 *
 * When started, a thread is spawned to asynchronously
 * process register reads/writes and memory writes.
 *
 * The idea is to simulate the asynch device operation by
 * passing messages to the thread, which maintains its
 * own state representing the current state of the device.
 * Some messages require replies, which are passed via atomics.
 * WaitableEvent is used to signal when messages are passed,
 * and also when results are available.
 *
 * So a typical register read is:
 *
 *   Main thread                 device thread
 * ->DevInterface->Read
 *     FakeDev->ReadRegister
 *       create event/result
 *       FakeDev->send
 *           Add msg to queue
 *               signal  - - -> FakeDev->Run
 *                                read msg from queue
 *                                FakeDev->ReadRegActual
 *             result < - - - -
 *             event  < - - - -
 *     return result
 */
#include "hps/hal/fake_dev.h"

#include <atomic>
#include <deque>
#include <iostream>
#include <map>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/lock.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/simple_thread.h>

#include "hps/hps_reg.h"
#include "hps/utils.h"

namespace hps {

/*
 * SimDev is an internal class (implementing DevInterface) that
 * forwards calls to the simulator.
 */
class SimDev : public DevInterface {
 public:
  explicit SimDev(scoped_refptr<FakeDev> device) : device_(device) {}
  virtual ~SimDev() {}

  bool ReadDevice(uint8_t cmd, uint8_t* data, size_t len) override {
    return this->device_->ReadDevice(cmd, data, len);
  }

  bool WriteDevice(uint8_t cmd, const uint8_t* data, size_t len) override {
    return this->device_->WriteDevice(cmd, data, len);
  }

  size_t BlockSizeBytes() override { return this->device_->BlockSizeBytes(); }

 private:
  // Reference counted simulator object.
  scoped_refptr<FakeDev> device_;
};

FakeDev::~FakeDev() {
  // If thread is running, send a request to terminate it.
  if (SimpleThread::HasBeenStarted() && !SimpleThread::HasBeenJoined()) {
    this->MsgStop();
    this->Join();
  }
}

// Start the simulator.
void FakeDev::Start() {
  CHECK(!SimpleThread::HasBeenStarted());
  SimpleThread::Start();
}

bool FakeDev::ReadDevice(uint8_t cmd, uint8_t* data, size_t len) {
  // Clear the whole buffer.
  for (int i = 0; i < len; i++) {
    data[i] = 0;
  }
  if ((cmd & 0x80) != 0) {
    // Register read.
    uint16_t value = this->ReadRegister(cmd & 0x7F);
    // Store the value of the register into the buffer.
    if (len > 0) {
      data[0] = (value >> 8) & 0xFF;
      if (len > 1) {
        data[1] = value & 0xFF;
      }
    }
  } else {
    // No memory read.
    return false;
  }
  return true;
}

bool FakeDev::WriteDevice(uint8_t cmd, const uint8_t* data, size_t len) {
  if ((cmd & 0x80) != 0) {
    if (len != 0) {
      // Register write.
      int reg = cmd & 0x7F;
      uint16_t value = data[0] << 8;
      if (len > 1) {
        value |= data[1];
      }
      this->WriteRegister(reg, value);
    }
  } else if ((cmd & 0xC0) == 0) {
    // Memory write.
    return this->WriteMemory(cmd & 0x3F, data, len);
  } else {
    // Unknown command.
    return false;
  }
  return true;
}

// Switch to the stage selected, and set up any flags or config.
// Depending on the stage, the HPS module supports different
// registers and attributes.
void FakeDev::SetStage(Stage s) {
  this->stage_ = s;
  switch (s) {
    case kFault:
      this->bank_ = 0;
      break;
    case kStage0:
      this->bank_ = 0x0001;
      break;
    case kStage1:
      this->bank_ = 0x0002;
      break;
    case kAppl:
      this->bank_ = 0;
      break;
  }
}

// Run reads the message queue and processes each message.
void FakeDev::Run() {
  // Initial startup.
  // Check for boot fault.
  if (this->Flag(kBootFault)) {
    this->SetStage(kFault);
  } else {
    this->SetStage(kStage0);
  }
  for (;;) {
    // Main message loop.
    this->ev_.Wait();
    this->ev_.Reset();
    for (;;) {
      // Read all messages available.
      Msg m;
      {
        base::AutoLock l(this->qlock_);
        if (this->q_.empty()) {
          break;
        }
        // Retrieve the next message.
        m = this->q_.front();
        this->q_.pop_front();
      }
      switch (m.cmd) {
        case kStop:
          // Exit simulator.
          return;
        case kReadReg:
          // Read a register and return the result.
          m.result->store(this->ReadRegActual(HpsReg(m.reg)));
          m.sig->Signal();
          break;
        case kWriteReg:
          // Write a register.
          this->WriteRegActual(HpsReg(m.reg), m.value);
          break;
        case kWriteMem:
          // Memory write request.
          m.result->store(this->WriteMemActual(m.reg, m.data, m.length));
          m.sig->Signal();
          // Re-enable the bank.
          // TODO(amcrae): Add delay to simulate a flash write.
          // This should be done in a separate thread so that
          // registers can be read while the memory is being
          // written.
          this->bank_.fetch_or(1 << m.reg);
          break;
      }
    }
  }
}

uint16_t FakeDev::ReadRegister(int r) {
  std::atomic<uint16_t> res(0);
  base::WaitableEvent ev;
  this->Send(Msg(Cmd::kReadReg, r, 0, &ev, &res));
  ev.Wait();
  return res.load();
}

void FakeDev::WriteRegister(int r, uint16_t v) {
  this->Send(Msg(Cmd::kWriteReg, r, v, nullptr, nullptr));
}

// At the start of the write, clear the bank ready bit.
// The simulator will set it again once the memory write completes.
bool FakeDev::WriteMemory(int base, const uint8_t* mem, size_t len) {
  // Ensure minimum length (4 bytes of address).
  if (len < sizeof(uint32_t)) {
    return false;
  }
  this->bank_.fetch_and(~(1 << base));
  std::atomic<uint16_t> res(0);
  base::WaitableEvent ev;
  Msg m(Cmd::kWriteMem, base, 0, &ev, &res);
  m.data = mem;
  m.length = len;
  this->Send(m);
  ev.Wait();
  // Device response is number of bytes written.
  // Return true if write succeeded.
  return res.load() == len;
}

uint16_t FakeDev::ReadRegActual(HpsReg reg) {
  uint16_t v = 0;
  switch (reg) {
    case HpsReg::kMagic:
      v = kHpsMagic;
      break;
    case HpsReg::kHwRev:
      if (this->stage_ == kStage0) {
        v = 0x0101;  // Version return in stage0.
      }
      break;
    case HpsReg::kSysStatus:
      if (this->stage_ == kFault) {
        v = hps::R2::kFault;
        break;
      }
      v = hps::R2::kOK;
      if (this->Flag(kApplNotVerified)) {
        v |= hps::R2::kApplNotVerified;
      } else {
        v |= hps::R2::kApplVerified;
      }
      if (this->Flag(kWpOff)) {
        v |= hps::R2::kWpOff;
      } else {
        v |= hps::R2::kWpOn;
      }
      if (this->stage_ == kStage1) {
        v |= hps::R2::kStage1;
        if (this->Flag(kSpiNotVerified)) {
          v |= hps::R2::kSpiNotVerified;
        } else {
          v |= hps::R2::kSpiVerified;
        }
      }
      if (this->stage_ == Stage::kAppl) {
        v |= hps::R2::kAppl;
      }
      break;

    case HpsReg::kBankReady:
      v = this->bank_.load();
      break;

    case HpsReg::kF1:
      if (this->feature_on_ & hps::R7::kFeature1Enable) {
        v = hps::RFeat::kValid | this->f1_result_.load();
      }
      break;

    case HpsReg::kF2:
      if (this->feature_on_ & hps::R7::kFeature2Enable) {
        v = hps::RFeat::kValid | this->f2_result_.load();
      }
      break;

    case HpsReg::kFirmwareVersionHigh:
      // Firmware version, only returned in stage0 if the
      // application has been verified.
      if (this->stage_ == kStage0 && !this->Flag(kApplNotVerified)) {
        v = static_cast<uint16_t>(firmware_version_.load() >> 16);
      } else {
        v = 0xFFFF;
      }
      break;

    case HpsReg::kFirmwareVersionLow:
      // Firmware version, only returned in stage0 if the
      // application has been verified.
      if (this->stage_ == kStage0 && !this->Flag(kApplNotVerified)) {
        v = static_cast<uint16_t>(firmware_version_.load() & 0xFFFF);
      } else {
        v = 0xFFFF;
      }
      break;

    default:
      break;
  }
  VLOG(2) << "Read reg " << HpsRegToString(reg) << " value " << v;
  return v;
}

void FakeDev::WriteRegActual(HpsReg reg, uint16_t value) {
  VLOG(2) << "Write reg " << HpsRegToString(reg) << " value " << value;
  // Ignore everything except the command register.
  switch (reg) {
    case HpsReg::kSysCmd:
      if (value & hps::R3::kReset) {
        this->SetStage(kStage0);
      } else if (value & hps::R3::kLaunch) {
        // Only valid in stage0
        if (this->stage_ == kStage0) {
          this->SetStage(kStage1);
        }
      } else if (value & hps::R3::kEnable) {
        // Only valid in stage1
        if (this->stage_ == kStage1) {
          this->SetStage(kAppl);
        }
      }
      break;

    case HpsReg::kFeatEn:
      // Set the feature enable bit mask.
      this->feature_on_ = value;
      break;

    default:
      break;
  }
}

// Returns the number of bytes written.
// The length includes 4 bytes of prepended address.
uint16_t FakeDev::WriteMemActual(int bank, const uint8_t* data, size_t len) {
  if (this->Flag(kMemFail)) {
    return 0;
  }
  // Don't allow writes that exceed the max block size.
  if (len > (this->block_size_b_.load() + sizeof(uint32_t))) {
    return 0;
  }
  switch (this->stage_) {
    case kStage0:
      // Stage0 allows the MCU flash to be written.
      if (bank == 0) {
        this->bank_len_[bank] += len - sizeof(uint32_t);
        // Check if the fake needs to reset the not-verified bit.
        if (this->Flag(kResetApplVerification)) {
          this->Clear(kApplNotVerified);
        }
        // Check if the fake should increment the version.
        if (this->Flag(kIncrementVersion)) {
          this->Clear(kIncrementVersion);
          this->firmware_version_++;
        }
        return len;
      }
      break;
    case kStage1:
      // Stage1 allows the SPI flash to be written.
      if (bank == 1) {
        this->bank_len_[bank] += len - sizeof(uint32_t);
        // Check if the fake needs to reset the not-verified bit.
        if (this->Flag(kResetSpiVerification)) {
          this->Clear(kSpiNotVerified);
        }
        return len;
      }
      break;
    default:
      break;
  }
  return 0;
}

size_t FakeDev::GetBankLen(hps::HpsBank bank) {
  base::AutoLock l(this->bank_lock_);
  return this->bank_len_[static_cast<int>(bank)];
}

void FakeDev::MsgStop() {
  this->Send(Msg(Cmd::kStop, 0, 0, nullptr, nullptr));
}

void FakeDev::Send(const Msg& m) {
  base::AutoLock l(this->qlock_);
  this->q_.push_back(m);
  this->ev_.Signal();
}

// Return a DevInterface connected to the simulated device.
std::unique_ptr<DevInterface> FakeDev::CreateDevInterface() {
  return std::unique_ptr<DevInterface>(std::make_unique<SimDev>(this));
}

// Static factory method to create and start an instance of a fake device.
scoped_refptr<FakeDev> FakeDev::Create() {
  auto fake_dev = base::MakeRefCounted<FakeDev>();
  fake_dev->Start();
  return fake_dev;
}

}  // namespace hps
