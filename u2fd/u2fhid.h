// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2FHID_H_
#define U2FD_U2FHID_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <base/timer/timer.h>
#include <brillo/errors/error.h>
#include <metrics/metrics_library.h>
#include <trunks/cr50_headers/u2f.h>

#include "u2fd/hid_interface.h"
#include "u2fd/u2f_adpu.h"
#include "u2fd/u2f_msg_handler.h"
#include "u2fd/user_state.h"

namespace u2f {

// Mandatory length of the U2F HID report.
constexpr size_t kU2fReportSize = 64;

// HID frame CMD/SEQ byte definitions.
constexpr uint8_t kFrameTypeMask = 0x80;
constexpr uint8_t kFrameTypeInit = 0x80;
// when bit 7 is not set, the frame type is CONTinuation.

// INIT command parameters
constexpr uint32_t kCidBroadcast = -1U;
constexpr size_t kInitNonceSize = 8;

constexpr uint8_t kCapFlagLock = 0x02;

constexpr size_t kMaxPayloadSize = (64 - 7 + 128 * (64 - 5));  // 7609 bytes

// U2fHid emulates U2FHID protocol on top of the TPM U2F implementation.
// The object reads the HID report sent by the HIDInterface passed to the
// constructor, parses it and extracts the U2FHID command. If this is a U2F
// message, finally sends the raw U2F APDU to the |transmit_func| callback
// passed to the constructor. It returns the final result (response APDU or
// error code) inside an HID report through the HIDInterface.
class U2fHid {
 public:
  // U2FHID Command codes
  enum class U2fHidCommand : uint8_t {
    kPing = 1,
    kMsg = 3,
    kLock = 4,
    kVendorSysInfo = 5,
    kInit = 6,
    kWink = 8,
    kError = 0x3f,
  };

  // U2FHID error codes
  enum class U2fHidError : uint8_t {
    kNone = 0,
    kInvalidCmd = 1,
    kInvalidPar = 2,
    kInvalidLen = 3,
    kInvalidSeq = 4,
    kMsgTimeout = 5,
    kChannelBusy = 6,
    kLockRequired = 10,
    kInvalidCid = 11,
    kOther = 127,
  };

  // Create a new virtual U2F HID Device. Does not take ownership of
  // msg_handler, which must outlive this instance.
  U2fHid(std::unique_ptr<HidInterface> hid,
         U2fMessageHandler* msg_handler);
  ~U2fHid();
  bool Init();

 private:
  // U2FHID protocol commands implementation.
  void CmdInit(uint32_t cid, const std::string& payload);
  int CmdLock(std::string* resp);
  int CmdMsg(std::string* resp);
  int CmdPing(std::string* resp);
  int CmdSysInfo(std::string* resp);

  // Fully resets the state of the possibly on-going U2FHID transaction.
  void ClearTransaction();

  // Sends back a U2FHID report with just the |errcode| error code inside
  // on channel |cid|.
  // If |clear| is set, clear the transaction state at the same time.
  void ReturnError(U2fHidError errcode, uint32_t cid, bool clear);

  // Called when we reach the deadline for the on-going transaction.
  void TransactionTimeout();

  // Called when we reach the deadline for an unreleased channel lock.
  void LockTimeout();

  // Sends back a U2FHID report indicating success and carrying the response
  // payload |resp|.
  void ReturnResponse(const std::string& resp);

  // Executes the action requested by the command contained in the current
  // transaction.
  void ExecuteCmd();

  // Parses the HID report contained in |report| and append the content to the
  // current U2FHID transaction or create a new one.
  void ProcessReport(const std::string& report);


  std::unique_ptr<HidInterface> hid_;
  uint32_t free_cid_;
  uint32_t locked_cid_;
  base::OneShotTimer lock_timeout_;
  U2fMessageHandler* msg_handler_;

  class HidPacket;
  class HidMessage;
  struct Transaction;

  std::unique_ptr<Transaction> transaction_;

  DISALLOW_COPY_AND_ASSIGN(U2fHid);
};

}  // namespace u2f

#endif  // U2FD_U2FHID_H_
