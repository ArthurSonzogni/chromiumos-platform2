// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Fake device for HPS testing.
 */
#ifndef HPS_HAL_FAKE_DEV_H_
#define HPS_HAL_FAKE_DEV_H_

#include <atomic>
#include <deque>
#include <map>
#include <memory>

#include <base/memory/ref_counted.h>
#include <base/synchronization/lock.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/simple_thread.h>

#include "hps/dev.h"
#include "hps/hps_reg.h"

namespace hps {

/*
 * FakeDev is an class that when started, spawns a thread to
 * asynchronously process register reads/writes and memory writes to
 * simulate the HPS hardware.
 * A separate thread is used to simulate the latency and concurrency of
 * the real device.
 *
 * A set of flags defines behaviour of the device (such as forced errors etc.).
 */
class FakeDev : public base::RefCounted<FakeDev>, base::SimpleThread {
 public:
  FakeDev()
      : SimpleThread("HPS Simulator"),
        stage_(Stage::kFault),
        feature_on_(0),
        bank_(0),
        flags_(0),
        firmware_version_(0),
        block_size_b_(256),
        f0_result_(0),
        f1_result_(0) {}
  // Flags for controlling behaviour. Multiple flags can be set,
  // controlling how the fake responds under test conditions.
  enum class Flags {
    // Set FAULT bit at boot.
    kBootFault = 0,
    // Set MCU RW not verified status bit.
    kApplNotVerified = 1,
    // Set SPI flash not verified status bit.
    kSpiNotVerified = 2,
    // Set WP bit as off.
    kWpOff = 3,
    // Fail any memory writes.
    kMemFail = 4,
    // If MCU download occurs, reset the RW not-verified flag.
    kResetApplVerification = 5,
    // If SPI download occurs, reset the SPI not-verified flag.
    kResetSpiVerification = 6,
    // When a RW download occurs, increment the firmware version number.
    kIncrementVersion = 7,
  };
  bool ReadDevice(uint8_t cmd, uint8_t* data, size_t len);
  bool WriteDevice(uint8_t cmd, const uint8_t* data, size_t len);
  size_t BlockSizeBytes() { return this->block_size_b_.load(); }
  void Run() override;
  void Start();
  void SkipBoot() { this->SetStage(Stage::kAppl); }
  void Set(Flags f) {
    this->flags_.fetch_or(static_cast<uint16_t>(1 << static_cast<int>(f)));
  }
  void Clear(Flags f) {
    this->flags_.fetch_and(~static_cast<uint16_t>(1 << static_cast<int>(f)));
  }
  void SetVersion(uint32_t version) { this->firmware_version_ = version; }
  void SetBlockSizeBytes(size_t sz) { this->block_size_b_ = sz; }
  void SetF0Result(int8_t result) { this->f0_result_ = result; }
  void SetF1Result(int8_t result) { this->f1_result_ = result; }
  size_t GetBankLen(hps::HpsBank bank);
  // Return a DevInterface accessing the simulator.
  std::unique_ptr<DevInterface> CreateDevInterface();
  // Create an instance of a simulator.
  static scoped_refptr<FakeDev> Create();

 private:
  // Message code identifying the type of message passed
  // to the simulation thread.
  enum Cmd {
    kStop,
    kReadReg,
    kWriteReg,
    kWriteMem,
  };
  // Message structure. Messages are delivered to the simulator thread,
  // which processes the request and responds if appropriate (via a
  // WaitableEvent signal).
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
    const uint8_t* data;
    size_t length;
  };

  friend class base::RefCounted<FakeDev>;
  ~FakeDev() override;
  uint16_t ReadRegister(int r);
  void WriteRegister(int r, uint16_t v);
  bool WriteMemory(int base, const uint8_t* mem, size_t len);
  bool Flag(Flags f) {
    return (this->flags_.load() & (1 << static_cast<int>(f))) != 0;
  }
  // Current stage (phase) of the device.
  // The device behaves differently in different stages.
  enum class Stage {
    kFault,
    kStage0,
    kStage1,
    kAppl,
  };
  void SetStage(Stage s);
  uint16_t ReadRegActual(HpsReg reg);
  void WriteRegActual(HpsReg reg, uint16_t value);
  uint16_t WriteMemActual(int base, const uint8_t* mem, size_t len);
  void MsgStop();
  void Send(const Msg& m);
  std::deque<Msg> q_;                // Message queue
  std::map<int, size_t> bank_len_;   // Count of writes to banks.
  base::Lock bank_lock_;             // Lock for bank_len_
  base::Lock qlock_;                 // Lock for queue
  base::WaitableEvent ev_;           // Signal for messages available
  Stage stage_;                      // Current stage of the device
  uint16_t feature_on_;              // Enabled features.
  std::atomic<uint16_t> bank_;       // Current memory bank readiness
  std::atomic<uint16_t> flags_;      // Behaviour flags
  std::atomic<uint32_t> firmware_version_;  // Firmware version
  std::atomic<size_t> block_size_b_;  // Write block size.
  std::atomic<int8_t> f0_result_;     // Result for feature 0
  std::atomic<int8_t> f1_result_;     // Result for feature 1
};

}  // namespace hps

#endif  // HPS_HAL_FAKE_DEV_H_
