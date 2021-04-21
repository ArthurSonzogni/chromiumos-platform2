// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Fake HPS device.
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
 * ->Device->read
 *     DevImpl->readReg
 *       create event/result
 *       DevImpl->send
 *           Add msg to queue
 *               signal  - - -> DevImpl->Run
 *                                read msg from queue
 *                                DevImpl->readRegActual
 *             result < - - - -
 *             event  < - - - -
 *     return result
 */
#include <atomic>
#include <deque>
#include <iostream>
#include <vector>

#include <base/check.h>
#include <base/synchronization/lock.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/simple_thread.h>

#include "hps/lib/fake_dev.h"
#include "hps/lib/hps_reg.h"

namespace {

// Message type.
enum Cmd {
  kStop,
  kReadReg,
  kWriteReg,
  kWriteMem,
};

// Message to be delivered to device simulator thread.
struct Msg {
  Msg() : cmd(Cmd::kStop), reg(0), value(0), sig(nullptr), result(nullptr) {}
  Msg(Cmd c,
      int r,
      uint16_t v,
      base::WaitableEvent* e,
      std::atomic<uint16_t>* res)
      : cmd(c), reg(r), value(v), sig(e), result(res) {}
  Cmd cmd;
  int reg;  // Or bank for memory write
  uint16_t value;
  base::WaitableEvent* sig;
  std::atomic<uint16_t>* result;
  std::vector<uint8_t> mem;  // Used for memory write.
};

}  // namespace

namespace hps {

/*
 * DevImpl is an internal class that when started,
 * will spawn a thread to asynchronously
 * process register reads/writes and memory writes.
 * A separate thread is used to simulate the latency
 * and concurrency of the real device.
 * A set of flags passed at the start defines
 * behaviour of the device (such as forced errors etc.).
 */
class DevImpl : public base::SimpleThread {
 public:
  DevImpl()
      : SimpleThread("fake HPS"),
        stage_(Stage::kBootFault),
        flags_(0),
        bank_(0) {}
  virtual ~DevImpl();
  virtual void Run();
  void Start(uint flags);
  uint16_t readReg(int r);
  void writeReg(int r, uint16_t v) {
    this->send(Msg(Cmd::kWriteReg, r, v, nullptr, nullptr));
  }
  bool writeMem(int base, const std::vector<uint8_t>& mem);
  // Current stage of the device.
  // The stage defines the registers and status values supported.
  enum Stage {
    kBootFault,
    kStage0,
    kStage1,
    kAppl,
  };

 private:
  void SetStage(Stage s);
  uint16_t readRegActual(int);
  void writeRegActual(int, uint16_t);
  uint16_t writeMemActual(int base, const std::vector<uint8_t>& mem);
  void MsgStop() { this->send(Msg(Cmd::kStop, 0, 0, nullptr, nullptr)); }
  void send(const Msg m) {
    base::AutoLock l(this->qlock_);
    this->q_.push_back(m);
    this->ev_.Signal();
  }
  std::deque<Msg> q_;           // Message queue
  base::Lock qlock_;            // Lock for queue
  base::WaitableEvent ev_;      // Signal for messages available
  Stage stage_;                 // Current stage of the device
  uint flags_;                  // Behaviour flags.
  std::atomic<uint16_t> bank_;  // Current memory bank readiness
};

DevImpl::~DevImpl() {
  // If thread is running, ask to terminate it.
  if (SimpleThread::HasBeenStarted() && !SimpleThread::HasBeenJoined()) {
    // Send message to terminate thread.
    this->MsgStop();
    this->Join();
  }
}

// Reset and start the device simulator.
void DevImpl::Start(uint flags) {
  CHECK(!SimpleThread::HasBeenStarted());
  this->flags_ = flags;
  SimpleThread::Start();
}

// Switch to the stage selected, and set up any flags or config.
void DevImpl::SetStage(Stage s) {
  this->stage_ = s;
  switch (s) {
    case Stage::kBootFault:
      this->bank_ = 0;
      break;
    case Stage::kStage0:
      this->bank_ = 0x0001;
      break;
    case Stage::kStage1:
      this->bank_ = 0x0002;
      break;
    case Stage::kAppl:
      this->bank_ = 0;
      break;
  }
}

// Run reads the message queue and processes each message.
void DevImpl::Run() {
  // Initial startup.
  // Check for boot fault.
  if (this->flags_ & FakeDev::Flags::kBootFault) {
    this->SetStage(Stage::kBootFault);
  } else {
    this->SetStage(Stage::kStage0);
  }
  for (;;) {
    // Main message loop.
    this->ev_.Wait();
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
          // Exit thread.
          return;
        case kReadReg:
          // Read a register and return the result.
          m.result->store(this->readRegActual(m.reg));
          m.sig->Signal();
          break;
        case kWriteReg:
          // Write a register.
          this->writeRegActual(m.reg, m.value);
          break;
        case kWriteMem:
          // Memory write request.
          m.result->store(this->writeMemActual(m.reg, m.mem));
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

uint16_t DevImpl::readReg(int r) {
  std::atomic<uint16_t> res(0);
  base::WaitableEvent ev;
  this->send(Msg(Cmd::kReadReg, r, 0, &ev, &res));
  ev.Wait();
  return res.load();
}

// At the start of the write, clear the bank ready bit.
// The handler will set it again once the memory write completes.
bool DevImpl::writeMem(int base, const std::vector<uint8_t>& mem) {
  this->bank_.fetch_and(~(1 << base));
  std::atomic<uint16_t> res(0);
  base::WaitableEvent ev;
  Msg m(Cmd::kWriteMem, base, 0, &ev, &res);
  m.mem = mem;
  this->send(m);
  ev.Wait();
  // Device response is 1 for OK, 0 for not OK.
  return res.load() != 0;
}

uint16_t DevImpl::readRegActual(int reg) {
  uint16_t v = 0;
  switch (reg) {
    case HpsReg::kMagic:
      v = kHpsMagic;
      break;
    case HpsReg::kHwRev:
      if (this->stage_ == Stage::kStage0) {
        v = 0x0101;  // Version return in stage0.
      }
      break;
    case HpsReg::kSysStatus:
      if (this->stage_ == Stage::kBootFault) {
        v = hps::R2::kFault;
        break;
      }
      v = hps::R2::kOK;
      if (this->flags_ & FakeDev::Flags::kApplNotVerified) {
        v |= hps::R2::kApplNotVerified;
      } else {
        v |= hps::R2::kApplVerified;
      }
      if (this->flags_ & FakeDev::Flags::kWpOff) {
        v |= hps::R2::kWpOff;
      } else {
        v |= hps::R2::kWpOn;
      }
      if (this->stage_ != Stage::kStage0) {
        v |= hps::R2::kApplRun;
      }
      break;

    case HpsReg::kBankReady:
      v = this->bank_.load();
      break;

    default:
      break;
  }
  return v;
}

void DevImpl::writeRegActual(int reg, uint16_t value) {
  // Ignore everything except the command register.
  switch (reg) {
    case HpsReg::kSysCmd:
      if (value & hps::R3::kReset) {
        this->SetStage(Stage::kStage0);
      }
      if (value & hps::R3::kLaunch) {
        // Only valid in stage0
        if (this->stage_ == Stage::kStage0) {
          this->SetStage(Stage::kStage1);
        }
      }
      break;
    default:
      break;
  }
}

// Returns 1 for OK, 0 for fault.
// TODO(amcrae): Store the memory written for each bank so
// it can be checked against what was requested to be written.
uint16_t DevImpl::writeMemActual(int bank, const std::vector<uint8_t>& mem) {
  switch (this->stage_) {
    case Stage::kStage0:
      // Stage1 allows the MCU flash.
      if (bank == 0) {
        return 1;
      }
      break;
    case Stage::kStage1:
      // Stage1 allows the SPI flash.
      if (bank == 1) {
        return 1;
      }
      break;
    default:
      break;
  }
  return 0;
}

// Main FakeDev class.

FakeDev::FakeDev() {}

FakeDev::~FakeDev() {}

/*
 * Read from registers.
 */
bool FakeDev::read(uint8_t cmd, std::vector<uint8_t>* data) {
  // Clear the whole buffer.
  for (int i = 0; i < data->size(); i++) {
    (*data)[i] = 0;
  }
  if ((cmd & 0x80) != 0) {
    // Register read.
    uint16_t value = this->device_->readReg(cmd & 0x7F);
    // Store the value of the register into the buffer.
    if (data->size() > 0) {
      (*data)[0] = value >> 8;
      if (data->size() > 1) {
        (*data)[1] = value;
      }
    }
  }
  return true;
}

/*
 * Write to registers or memory.
 */
bool FakeDev::write(uint8_t cmd, const std::vector<uint8_t>& data) {
  if ((cmd & 0x80) != 0) {
    if (data.size() != 0) {
      // Register write.
      int reg = cmd & 0x7F;
      uint16_t value = data[0] << 8;
      if (data.size() > 1) {
        value |= data[1];
      }
      this->device_->writeReg(reg, value);
    }
  } else if ((cmd & 0xC0) == 0) {
    // Memory write.
    return this->device_->writeMem(cmd & 0x3F, data);
  } else {
    // Unknown command.
    return false;
  }
  return true;
}

// Start the fake.
void FakeDev::Start(uint flags) {
  this->device_.reset(new DevImpl);
  this->device_->Start(flags);
}

}  // namespace hps
