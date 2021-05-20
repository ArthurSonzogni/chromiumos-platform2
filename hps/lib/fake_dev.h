// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Fake device for HPS testing.
 */
#ifndef HPS_LIB_FAKE_DEV_H_
#define HPS_LIB_FAKE_DEV_H_

#include <atomic>
#include <deque>
#include <memory>

#include <base/memory/ref_counted.h>
#include <base/synchronization/lock.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/simple_thread.h>

#include "hps/lib/dev.h"
#include "hps/lib/hps_reg.h"

namespace hps {

/*
 * FakeHps is an class that when started, spawns a thread to
 * asynchronously process register reads/writes and memory writes.
 * A separate thread is used to simulate the latency and concurrency of
 * the real device.
 *
 * A set of flags defines behaviour of the device (such as forced errors etc.).
 */
class FakeHps : public base::RefCounted<FakeHps>, base::SimpleThread {
 public:
  FakeHps()
      : SimpleThread("HPS Simulator"),
        stage_(Stage::kFault),
        feature_on_(0),
        bank_(0),
        flags_(0),
        version_(0),
        f1_result_(0),
        f2_result_(0) {}
  // Flags for controlling behaviour. Multiple flags can be set,
  // controlling how the fake responds under test conditions.
  enum Flags {
    kBootFault = 0,
    kApplNotVerified = 1,
    kSpiNotVerified = 2,
    kWpOff = 3,
    kMemFail = 4,
  };
  bool Read(uint8_t cmd, uint8_t* data, size_t len);
  bool Write(uint8_t cmd, const uint8_t* data, size_t len);
  void Run() override;
  void Start();
  void SkipBoot() { this->SetStage(Stage::kAppl); }
  void Set(uint16_t set) { this->flags_.fetch_or(set); }
  void Clear(uint16_t clear) { this->flags_.fetch_and(~clear); }
  void SetVersion(uint16_t version) { this->version_ = version; }
  void SetF1Result(uint16_t result) { this->f1_result_ = result & 0x7FFF; }
  void SetF2Result(uint16_t result) { this->f2_result_ = result & 0x7FFF; }
  // Return a DevInterface accessing the simulator.
  std::unique_ptr<DevInterface> CreateDevInterface();
  // Create an instance of a simulator.
  static scoped_refptr<FakeHps> Create();

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

  friend class base::RefCounted<FakeHps>;
  virtual ~FakeHps();
  uint16_t ReadRegister(int r);
  void WriteRegister(int r, uint16_t v);
  bool WriteMemory(int base, const uint8_t* mem, size_t len);
  bool Flag(FakeHps::Flags f) { return (this->flags_.load() & (1 << f)) != 0; }
  // Current stage (phase) of the device.
  // The device behaves differently in different stages.
  enum Stage {
    kFault,
    kStage0,
    kStage1,
    kAppl,
  };
  void SetStage(Stage s);
  uint16_t ReadRegActual(int);
  void WriteRegActual(int, uint16_t);
  uint16_t WriteMemActual(int base, const uint8_t* mem, size_t len);
  void MsgStop();
  void Send(const Msg& m);
  std::deque<Msg> q_;                // Message queue
  base::Lock qlock_;                 // Lock for queue
  base::WaitableEvent ev_;           // Signal for messages available
  Stage stage_;                      // Current stage of the device
  uint16_t feature_on_;              // Enabled features.
  std::atomic<uint16_t> bank_;       // Current memory bank readiness
  std::atomic<uint16_t> flags_;      // Behaviour flags
  std::atomic<uint16_t> version_;    // Application version
  std::atomic<uint16_t> f1_result_;  // Result for feature 1
  std::atomic<uint16_t> f2_result_;  // Result for feature 2
};

}  // namespace hps

#endif  // HPS_LIB_FAKE_DEV_H_
