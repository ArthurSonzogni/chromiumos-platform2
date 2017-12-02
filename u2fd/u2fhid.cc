// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/strings/string_number_conversions.h>
#include <base/sys_byteorder.h>
#include <base/timer/timer.h>

#include "u2fd/u2fhid.h"

namespace {

// Mandatory length of the U2F HID report.
constexpr size_t kU2fReportSize = 64;

// Size of the payload for an INIT U2F HID report.
constexpr size_t kInitReportPayloadSize = 57;
// Size of the payload for a Continuation U2F HID report.
constexpr size_t kContReportPayloadSize = 59;

// HID frame CMD/SEQ byte definitions.
constexpr uint8_t kFrameTypeMask = 0x80;
constexpr uint8_t kFrameTypeInit = 0x80;
// when bit 7 is not set, the frame type is CONTinuation.

// INIT command parameters
constexpr uint32_t kCidBroadcast = -1U;
constexpr size_t kInitNonceSize = 8;

constexpr uint8_t kInterfaceVersion = 2;

constexpr uint8_t kCapFlagWink = 0x01;
constexpr uint8_t kCapFlagLock = 0x02;

constexpr int kU2fHidTimeoutMs = 500;

constexpr size_t kMaxPayloadSize = (64 - 7 + 128 * (64 - 5));  // 7609 bytes

// Maximum duration one can keep the channel lock as specified by the U2FHID
// specification
constexpr int kMaxLockDurationSeconds = 10;

// Response to the APDU requesting the U2F protocol version
constexpr char kSupportedU2fVersion[] = "U2F_V2";

// HID report descriptor for U2F interface.
constexpr uint8_t kU2fReportDesc[] = {
    0x06, 0xD0, 0xF1, /* Usage Page (FIDO Alliance), FIDO_USAGE_PAGE */
    0x09, 0x01,       /* Usage (U2F HID Auth. Device) FIDO_USAGE_U2FHID */
    0xA1, 0x01,       /* Collection (Application), HID_APPLICATION */
    0x09, 0x20,       /*  Usage (Input Report Data), FIDO_USAGE_DATA_IN */
    0x15, 0x00,       /*  Logical Minimum (0) */
    0x26, 0xFF, 0x00, /*  Logical Maximum (255) */
    0x75, 0x08,       /*  Report Size (8) */
    0x95, 0x40,       /*  Report Count (64), HID_INPUT_REPORT_BYTES */
    0x81, 0x02,       /*  Input (Data, Var, Abs), Usage */
    0x09, 0x21,       /*  Usage (Output Report Data), FIDO_USAGE_DATA_OUT */
    0x15, 0x00,       /*  Logical Minimum (0) */
    0x26, 0xFF, 0x00, /*  Logical Maximum (255) */
    0x75, 0x08,       /*  Report Size (8) */
    0x95, 0x40,       /*  Report Count (64), HID_OUTPUT_REPORT_BYTES */
    0x91, 0x02,       /*  Output (Data, Var, Abs), Usage */
    0xC0              /* End Collection */
};

}  // namespace

namespace u2f {

// U2FHID Command codes
enum class U2fHid::U2fHidCommand : uint8_t {
  kPing = 1,
  kMsg = 3,
  kLock = 4,
  kInit = 6,
  kWink = 8,
  kError = 0x3f,
};

// U2FHID error codes
enum class U2fHid::U2fHidError : uint8_t {
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

class U2fHid::HidPacket {
 public:
  explicit HidPacket(const std::string& report);

  bool IsValidFrame() const { return valid_; }

  bool IsInitFrame() const { return (tcs_ & kFrameTypeMask) == kFrameTypeInit; }

  uint32_t ChannelId() const { return cid_; }

  U2fHid::U2fHidCommand Command() const {
    return static_cast<U2fHidCommand>(tcs_ & ~kFrameTypeMask);
  }

  uint8_t SeqNumber() const { return tcs_ & ~kFrameTypeMask; }

  int PayloadIndex() const { return IsInitFrame() ? 8 : 6; }

  size_t MessagePayloadSize() const { return bcnt_; }

 private:
  bool valid_;
  uint32_t cid_;   // Channel Identifier
  uint8_t tcs_;    // type and command or sequence number
  uint16_t bcnt_;  // payload length as defined by U2fHID specification
};

U2fHid::HidPacket::HidPacket(const std::string& report)
    : valid_(false), cid_(0), tcs_(0), bcnt_(0) {
  // the report is prefixed by the report ID (we skip it below).
  if (report.size() != kU2fReportSize + 1) /* Invalid U2FHID report */
    return;

  // U2FHID frame bytes parsing.
  // As defined in the "FIDO U2F HID Protocol Specification":
  // An initialization packet is defined as
  //
  // Offset Length  Mnemonic  Description
  // 0      4       CID       Channel identifier
  // 4      1       CMD       Command identifier (bit 7 always set)
  // 5      1       BCNTH     High part of payload length
  // 6      1       BCNTL     Low part of payload length
  // 7      (s - 7) DATA      Payload data (s is the fixed packet size)
  // The command byte has always the highest bit set to distinguish it
  // from a continuation packet, which is described below.
  //
  // A continuation packet is defined as
  //
  // Offset Length  Mnemonic  Description
  // 0      4       CID       Channel identifier
  // 4      1       SEQ       Packet sequence 0x00..0x7f (bit 7 always cleared)
  // 5      (s - 5) DATA      Payload data (s is the fixed packet size)
  // With this approach, a message with a payload less or equal to (s - 7)
  // may be sent as one packet. A larger message is then divided into one or
  // more continuation packets, starting with sequence number 0 which then
  // increments by one to a maximum of 127.

  // the CID word is not aligned
  memcpy(&cid_, &report[1], sizeof(cid_));
  tcs_ = report[5];

  uint16_t raw_count;
  memcpy(&raw_count, &report[6], sizeof(raw_count));
  bcnt_ = base::NetToHost16(raw_count);

  valid_ = true;
}

class U2fHid::HidMessage {
 public:
  HidMessage(U2fHidCommand cmd, uint32_t cid) : cid_(cid), cmd_(cmd) {}

  // Appends |bytes| to the message payload.
  void AddPayload(const std::string& bytes);

  // Appends the single |byte| to the message payload.
  void AddByte(uint8_t byte);

  // Fills an HID report with the part of the message starting at |offset|.
  // Returns the offset of the remaining unused content in the message.
  int BuildReport(int offset, std::string* report_out);

 private:
  uint32_t cid_;
  U2fHidCommand cmd_;
  std::string payload_;
};

void U2fHid::HidMessage::AddPayload(const std::string& bytes) {
  payload_ += bytes;
}

void U2fHid::HidMessage::AddByte(uint8_t byte) {
  payload_.push_back(byte);
}

int U2fHid::HidMessage::BuildReport(int offset, std::string* report_out) {
  size_t data_size;

  // Serialize one chunk of the message in a 64-byte HID report
  // (see the HID report structure in HidPacket constructor)
  report_out->assign(
      std::string(reinterpret_cast<char*>(&cid_), sizeof(uint32_t)));
  if (offset == 0) {  // INIT message
    uint16_t bcnt = payload_.size();
    report_out->push_back(static_cast<uint8_t>(cmd_) | kFrameTypeInit);
    report_out->push_back(bcnt >> 8);
    report_out->push_back(bcnt & 0xff);
    data_size = kInitReportPayloadSize;
  } else {  // CONT message
    // Insert sequence number.
    report_out->push_back((offset - kInitReportPayloadSize) /
                          kContReportPayloadSize);
    data_size = kContReportPayloadSize;
  }
  data_size = std::min(data_size, payload_.size() - offset);
  *report_out += payload_.substr(offset, data_size);
  // Ensure the report is 64-B long
  report_out->insert(report_out->end(), kU2fReportSize - report_out->size(), 0);
  offset += data_size;

  VLOG(2) << "TX RPT ["
          << base::HexEncode(report_out->data(), report_out->size()) << "]";

  return offset != payload_.size() ? offset : 0;
}

struct U2fHid::Transaction {
  uint32_t cid = 0;
  U2fHidCommand cmd = U2fHidCommand::kError;
  size_t total_size = 0;
  int seq = 0;
  std::string payload;
  base::OneShotTimer timeout;
};

U2fHid::U2fHid(std::unique_ptr<HidInterface> hid,
               const TransmitApduCallback& transmit_func,
               const IgnoreButtonCallback& ignore_func)
    : hid_(std::move(hid)),
      transmit_apdu_(transmit_func),
      ignore_button_(ignore_func),
      free_cid_(1),
      locked_cid_(0) {
  transaction_ = std::make_unique<Transaction>();
  hid_->SetOutputReportHandler(
      base::Bind(&U2fHid::ProcessReport, base::Unretained(this)));
}

U2fHid::~U2fHid() = default;

bool U2fHid::Init() {
  return hid_->Init(kInterfaceVersion,
                    std::string(reinterpret_cast<const char*>(kU2fReportDesc),
                                sizeof(kU2fReportDesc)));
}

bool U2fHid::GetU2fVersion(std::string* version_out) {
  std::string ping(8, 0);
  std::string ver;

  // build the APDU for the command U2F_VERSION:
  // CLA INS P1  P2  Le
  // 00  03  00  00  00
  ping[1] = 0x03;
  int rc = transmit_apdu_.Run(ping, &ver);

  if (!rc) {
    // remove the 16-bit status code at the end
    *version_out = ver.substr(0, ver.length() - sizeof(uint16_t));
    VLOG(1) << "version " << *version_out;

    if (*version_out != kSupportedU2fVersion) {
      LOG(WARNING) << "Unsupported U2F version " << *version_out;
      return false;
    }
  }

  return !rc;
}

void U2fHid::ReturnError(U2fHidError errcode, uint32_t cid, bool clear) {
  HidMessage msg(U2fHidCommand::kError, cid);

  msg.AddByte(static_cast<uint8_t>(errcode));
  VLOG(1) << "ERROR/" << std::hex << static_cast<int>(errcode)
          << " CID:" << std::hex << cid;
  if (clear)
    transaction_ = std::make_unique<Transaction>();

  std::string report;
  msg.BuildReport(0, &report);
  hid_->SendReport(report);
}

void U2fHid::TransactionTimeout() {
  ReturnError(U2fHidError::kMsgTimeout, transaction_->cid, true);
}

void U2fHid::LockTimeout() {
  if (locked_cid_)
    LOG(WARNING) << "Cancelled lock CID:" << std::hex << locked_cid_;
  locked_cid_ = 0;
}

void U2fHid::ReturnResponse(const std::string& resp) {
  HidMessage msg(transaction_->cmd, transaction_->cid);
  int offset = 0;

  msg.AddPayload(resp);
  // Send all the chunks of the message (split in 64-B HID reports)
  do {
    std::string report;
    offset = msg.BuildReport(offset, &report);
    hid_->SendReport(report);
  } while (offset);
}

void U2fHid::ScanApdu(const std::string& payload) {
  if (payload.size() < 5)  // Unknown APDU format
    return;

  // Duration of the user presence persistence on the firmware side
  const base::TimeDelta kPresenceTimeout = base::TimeDelta::FromSeconds(10);

  // ISO7816-4:2005 APDU format: CLA INS P1 P2 [request data]
  char cla = payload[0];
  char ins = payload[1];
  char control = payload[4];
  constexpr char kU2fRegister = 1;         // U2F_REGISTER command code
  constexpr char kU2fAuthenticate = 2;     // U2F_AUTHENTICATE command code
  constexpr char kU2fAuthCheckOnly = 0x7;  // U2F_AUTH_CHECK_ONLY flags

  // Has the client requested the user physical presence ?
  if (cla == 0 && (ins == kU2fRegister ||
                   (ins == kU2fAuthenticate && control != kU2fAuthCheckOnly))) {
    brillo::ErrorPtr err;
    // Mask the next power button press for the UI
    ignore_button_.Run(kPresenceTimeout.ToInternalValue(), &err, -1);
  }
}

void U2fHid::CmdInit(uint32_t cid, const std::string& payload) {
  HidMessage msg(U2fHidCommand::kInit, cid);

  if (payload.size() != kInitNonceSize) {
    VLOG(1) << "Payload size " << payload.size();
    ReturnError(U2fHidError::kInvalidLen, cid, false);
    return;
  }

  VLOG(1) << "INIT CID:" << std::hex << cid << " NONCE "
          << base::HexEncode(payload.data(), payload.size());

  if (cid == kCidBroadcast) {  // Allocate Channel ID
    cid = free_cid_++;
    // Roll-over if needed
    if (free_cid_ == kCidBroadcast)
      free_cid_ = 1;
  }

  // Keep the nonce in the first 8 bytes
  msg.AddPayload(payload);

  std::string serial_cid(reinterpret_cast<char*>(&cid), sizeof(uint32_t));
  msg.AddPayload(serial_cid);

  // Append the versions : interface / major / minor / build
  msg.AddByte(kInterfaceVersion);
  msg.AddByte(0);
  msg.AddByte(0);
  msg.AddByte(0);
  // Append Capability flags
  // TODO(vpalatin) the Wink command is only outputting a trace for now,
  // do a real action or remove it.
  msg.AddByte(kCapFlagLock | kCapFlagWink);

  std::string report;
  msg.BuildReport(0, &report);
  hid_->SendReport(report);
}

int U2fHid::CmdPing(std::string* resp) {
  VLOG(1) << "PING len " << transaction_->total_size;

  // poke U2F version to simulate latency.
  std::string version;
  GetU2fVersion(&version);

  // send back the same content
  *resp = transaction_->payload.substr(0, transaction_->total_size);
  return transaction_->total_size;
}

int U2fHid::CmdLock(std::string* resp) {
  int duration = transaction_->payload[0];

  VLOG(1) << "LOCK " << duration << "s CID:" << std::hex << transaction_->cid;

  if (duration > kMaxLockDurationSeconds) {
    ReturnError(U2fHidError::kInvalidPar, transaction_->cid, true);
    return -EINVAL;
  }

  if (!duration) {
    lock_timeout_.Stop();
    locked_cid_ = 0;
  } else {
    locked_cid_ = transaction_->cid;
    lock_timeout_.Start(
        FROM_HERE,
        base::TimeDelta::FromSeconds(duration),
        base::Bind(&U2fHid::LockTimeout, base::Unretained(this)));
  }
  return 0;
}

int U2fHid::CmdWink(std::string* resp) {
  LOG(INFO) << "WINK!";
  return 0;
}

int U2fHid::CmdMsg(std::string* resp) {
  ScanApdu(transaction_->payload);
  return transmit_apdu_.Run(transaction_->payload, resp);
}

void U2fHid::ExecuteCmd() {
  int rc;
  std::string resp;

  transaction_->timeout.Stop();
  switch (transaction_->cmd) {
    case U2fHidCommand::kMsg:
      rc = CmdMsg(&resp);
      break;
    case U2fHidCommand::kPing:
      rc = CmdPing(&resp);
      break;
    case U2fHidCommand::kLock:
      rc = CmdLock(&resp);
      break;
    case U2fHidCommand::kWink:
      rc = CmdWink(&resp);
      break;
    default:
      LOG(WARNING) << "Unknown command " << std::hex
                   << static_cast<int>(transaction_->cmd);
      ReturnError(U2fHidError::kInvalidCmd, transaction_->cid, true);
      return;
  }

  if (rc >= 0)
    ReturnResponse(resp);

  // we are done with this transaction
  transaction_ = std::make_unique<Transaction>();
}

void U2fHid::ProcessReport(const std::string& report) {
  HidPacket pkt(report);

  VLOG(2) << "RX RPT/" << report.size() << " ["
          << base::HexEncode(report.data(), report.size()) << "]";
  if (!pkt.IsValidFrame())
    return;  // Invalid report

  // Check frame validity
  if (pkt.ChannelId() == 0) {
    VLOG(1) << "No frame should use channel 0";
    ReturnError(U2fHidError::kInvalidCid,
                pkt.ChannelId(),
                pkt.ChannelId() == transaction_->cid);
    return;
  }

  if (pkt.IsInitFrame() && pkt.Command() == U2fHidCommand::kInit) {
    if (pkt.ChannelId() == transaction_->cid) {
      // Abort an ongoing multi-packet transaction
      VLOG(1) << "Transaction cancelled on CID:" << std::hex << pkt.ChannelId();
      transaction_ = std::make_unique<Transaction>();
    }
    // special case: INIT should not interrupt other commands
    CmdInit(pkt.ChannelId(), report.substr(pkt.PayloadIndex(), kInitNonceSize));
    return;
  }
  // not an INIT command from here

  if (pkt.IsInitFrame()) {  // INIT frame type (not the INIT command)
    if (pkt.ChannelId() == kCidBroadcast) {
      VLOG(1) << "INIT command not on broadcast CID:" << std::hex
              << pkt.ChannelId();
      ReturnError(U2fHidError::kInvalidCid, pkt.ChannelId(), false);
      return;
    }
    if (locked_cid_ && pkt.ChannelId() != locked_cid_) {
      // somebody else has the lock
      VLOG(1) << "channel locked by CID:" << std::hex << locked_cid_;
      ReturnError(U2fHidError::kChannelBusy, pkt.ChannelId(), false);
      return;
    }
    if (transaction_->cid && (pkt.ChannelId() != transaction_->cid)) {
      VLOG(1) << "channel used by CID:" << std::hex << transaction_->cid;
      ReturnError(U2fHidError::kChannelBusy, pkt.ChannelId(), false);
      return;
    }
    if (transaction_->cid) {
      VLOG(1) << "CONT frame expected";
      ReturnError(U2fHidError::kInvalidSeq, pkt.ChannelId(), true);
      return;
    }
    if (pkt.MessagePayloadSize() > kMaxPayloadSize) {
      VLOG(1) << "Invalid length " << pkt.MessagePayloadSize();
      ReturnError(U2fHidError::kInvalidLen, pkt.ChannelId(), true);
      return;
    }

    transaction_->timeout.Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(kU2fHidTimeoutMs),
        base::Bind(&U2fHid::TransactionTimeout, base::Unretained(this)));

    // record transaction parameters
    transaction_->cid = pkt.ChannelId();
    transaction_->total_size = pkt.MessagePayloadSize();
    transaction_->cmd = pkt.Command();
    transaction_->seq = 0;
    transaction_->payload =
        report.substr(pkt.PayloadIndex(), transaction_->total_size);
  } else {  // CONT Frame
    if (transaction_->cid == 0 || transaction_->cid != pkt.ChannelId()) {
      VLOG(1) << "invalid CONT";
      return;  // just ignore
    }
    if (transaction_->seq != pkt.SeqNumber()) {
      VLOG(1) << "invalid sequence " << static_cast<int>(pkt.SeqNumber())
              << " !=  " << transaction_->seq;
      ReturnError(U2fHidError::kInvalidSeq,
                  pkt.ChannelId(),
                  pkt.ChannelId() == transaction_->cid);
      return;
    }
    // reload timeout
    transaction_->timeout.Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(kU2fHidTimeoutMs),
        base::Bind(&U2fHid::TransactionTimeout, base::Unretained(this)));
    // record the payload
    transaction_->payload += report.substr(pkt.PayloadIndex());
    transaction_->seq++;
  }
  // Are we done with this transaction ?
  if (transaction_->payload.size() >= transaction_->total_size)
    ExecuteCmd();
}

}  // namespace u2f
