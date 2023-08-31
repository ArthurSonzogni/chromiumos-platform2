// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_PSR_CMD_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_PSR_CMD_H_

#include <iostream>
#include <linux/mei.h>
#include <linux/uuid.h>
#include <memory>
#include <optional>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace psr {

namespace mojom = ::ash::cros_healthd::mojom;

// PSR version major
static constexpr uint8_t kPsrVersionMajor = 2;
// PSR version minor
static constexpr uint8_t kPsrVersionMinor = 0;
// ODCA chain length.
static constexpr uint8_t kOdcaChainLen = 4;
// Get record command index.
static constexpr uint8_t kGetRecordCmdIdx = 10;
// Max timeout for read in seconds.
static constexpr uint8_t kMaxTimeoutSec = 10;
// UUID length.
static constexpr uint8_t kUuidLength = 16;
// Header Padding size
static constexpr uint8_t kPaddingSize = 20;
// Extended capability size.
static constexpr uint8_t kExtCapSize = 32;
// UPID platform ID length.
static constexpr uint8_t kUpidLength = 64;
// Genesis field info size.
static constexpr uint8_t kGenesisFieldInfoSize = 64;
// Max number of events.
static constexpr uint8_t kEventNumMax = 100;
// Get FW Capability index.
static constexpr uint8_t kGetFwCapsIdx = 3;
// FW Capability rule command.
static constexpr uint8_t kFwCapCmd = 2;
// Max signing length.
static constexpr uint16_t kMaxSignLen = 512;
// Max certificate chain size.
static constexpr uint16_t kMaxCertChainSize = 3000;
// Genesis data store info size.
static constexpr uint16_t kGenesisDataStoreInfoSize = 1024;
// Delay open in microseconds.
static constexpr uint32_t kDelayUSec = 1e6;

// Unique ID for PSR MEI requests.
const uuid_le kGuid = GUID_INIT(
    0xED6703FA, 0xD312, 0x4E8B, 0x9D, 0xDD, 0x21, 0x55, 0xBB, 0x2D, 0xEE, 0x65);

const uuid_le kHciGuid = GUID_INIT(
    0x8E6A6715, 0x9ABC, 0x4043, 0x88, 0xEF, 0x9E, 0x39, 0xC6, 0xF6, 0x3E, 0x0F);

enum LedgerCounterIndex {
  // Counter index for total S0 time in seconds.
  kS0Seconds = 0,
  // Counter index for S0 to SX events.
  kS0ToS5 = 1,
  kS0ToS4 = 2,
  kS0ToS3 = 3,
  kWarmReset = 4,
  kMax = 32,
};

enum EventType : uint8_t {
  kLogStart = 8,
  kLogEnd = 9,
  kMissing = 17,
  kInvalid = 18,
  kPrtcFailure = 19,
  kCsmeRecovery = 20,
  kCsmeDamState = 21,
  kCsmeUnlockState = 22,
  kSvnIncrease = 23,
  kFwVersionChanged = 24,
};

enum LogState {
  kNotStarted = 0,  // Default value.
  kStarted,
  kStopped,
};

enum SignAlgo {
  kEcdsaSha384 = 0,
};

enum Status {
  kSuccess = 0,
  kFeatureNotSupported = 1,
  kUpidDisabled = 2,
  kActionNotAllowed = 3,
  kInvalidInputParameter = 4,
  kInternalError = 5,
  kNotAllowedAfterEop = 6,
};

union MkhiHeader {
  struct {
    uint32_t group_id : 8;
    uint32_t command : 7;
    uint32_t response : 1;
    uint32_t reserved : 8;
    uint32_t result : 8;
  } fields;
  uint32_t data;
};

union RuleId {
  struct {
    uint32_t rule_type : 16;
    uint32_t feature_id : 8;
    uint32_t reserved : 8;
  } fields;
  uint32_t data;
};

struct FwCapsRequest {
  MkhiHeader header;
  RuleId rule_id;
};

struct FwCapsResp {
  MkhiHeader header;
  RuleId rule_id;
  uint8_t rule_data_len;
  uint8_t rule_data[4];
};

struct PsrVersion {
  uint16_t major;
  uint16_t minor;
};

struct FwVersion {
  uint16_t major;
  uint16_t minor;
  uint16_t hotfix;
  uint16_t build;
};

struct HeciHeader {
  unsigned char command;
  uint8_t padding;
  uint16_t length;
};

struct HeciGetRequest {
  HeciHeader header;
  uint8_t padding[20];
};

struct GenesisRecord {
  uint32_t genesis_date;
  uint8_t oem_info[kGenesisFieldInfoSize];
  uint8_t oem_make_info[kGenesisFieldInfoSize];
  uint8_t oem_model_info[kGenesisFieldInfoSize];
  uint8_t manufacture_country[kGenesisFieldInfoSize];
  uint8_t oem_data[kGenesisDataStoreInfoSize];
};

struct Event {
  EventType event_type;
  uint8_t padding[3];
  uint32_t timestamp;
  uint32_t data;
};

struct LedgerRecord {
  uint32_t ledger_counter[LedgerCounterIndex::kMax];
};

struct PlatformServiceRecord {
  uint8_t uuid[kUuidLength];
  uint8_t upid[kUpidLength];
  GenesisRecord genesis_info;
  uint8_t capabilities[kExtCapSize];
  LedgerRecord ledger_info;
  uint32_t events_count;
  Event events_info[kEventNumMax];
};

struct PsrHeciResp {
  HeciHeader header;
  Status status;
  LogState log_state;
  PsrVersion psr_version;
  PlatformServiceRecord psr_record;
  uint8_t padding[104];
  FwVersion fw_version;
  SignAlgo sign_algo;
  uint8_t signature[kMaxSignLen];
  uint16_t certificate_lengths[kOdcaChainLen];
  uint8_t certificates[kMaxCertChainSize];
};

// Platform Service Record (PSR) Command virtual class, used for testing.
class PsrCmdVirt {
 public:
  PsrCmdVirt() = default;
  PsrCmdVirt(const PsrCmdVirt&) = delete;
  PsrCmdVirt& operator=(PsrCmdVirt&) = delete;
  virtual ~PsrCmdVirt() = default;

  enum CmdStatus {
    kSuccess,
    kInvalidState,
    kInsufficentBuffer,
    kMeiSendErr,
    kMeiRecErr,
    kMeiOpenErr,
  };

  virtual bool MeiConnect() { return false; }
  virtual bool MeiSend(void* buffer, ssize_t buff_len) { return false; }
  virtual bool MeiReceive(std::vector<uint8_t>& buffer, ssize_t& buff_len) {
    return false;
  }
  virtual CmdStatus Transaction(HeciGetRequest& tx_buff, PsrHeciResp& rx_buff) {
    return kInvalidState;
  }
  virtual CmdStatus Check(FwCapsRequest& tx_buff, FwCapsResp& rx_buff) {
    return kInvalidState;
  }
  // Checks PSR is supported or not. Returns std::nullopt if any error occurs.
  virtual std::optional<bool> CheckPlatformServiceRecord() {
    return std::nullopt;
  }
  virtual std::string IdToHexString(uint8_t id[], int id_len) { return ""; }
};

// PSR Command Class.
class PsrCmd : public PsrCmdVirt {
 public:
  explicit PsrCmd(const char* mei_fp);
  PsrCmd(const PsrCmd&) = delete;
  PsrCmd& operator=(const PsrCmd&) = delete;
  virtual ~PsrCmd() = default;

  bool GetPlatformServiceRecord(PsrHeciResp& psr_blob);
  std::optional<bool> CheckPlatformServiceRecord() override;
  std::string IdToHexString(uint8_t id[], int id_len) override;

 private:
  bool MeiConnect() override;
  bool MeiSend(void* buffer, ssize_t buff_len) override;
  bool MeiReceive(std::vector<uint8_t>& buffer, ssize_t& buff_len) override;
  CmdStatus Transaction(HeciGetRequest& tx_buff, PsrHeciResp& rx_buff) override;
  CmdStatus Check(FwCapsRequest& tx_buff, FwCapsResp& rx_buff) override;

  const char* mei_fp_;
  int mei_fd_;
  struct mei_connect_client_data* mei_connect_data_;
};

}  // namespace psr
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_PSR_CMD_H_
