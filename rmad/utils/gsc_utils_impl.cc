// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/gsc_utils_impl.h>

#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "rmad/utils/cmd_utils_impl.h"
#include "rmad/utils/gsc_utils.h"

namespace rmad {

namespace {

constexpr char kGsctoolCmd[] = "gsctool";
// Constants for RSU.
const std::vector<std::string> kGetRsuChallengeArgv{kGsctoolCmd, "-a", "-r",
                                                    "-M"};
const std::vector<std::string> kSendRsuResponseArgv{kGsctoolCmd, "-a", "-r"};
constexpr char kRsuChallengeRegexp[] = R"(CHALLENGE=([[:alnum:]]{80}))";
// Constants for CCD info.
const std::vector<std::string> kGetCcdInfoArgv{kGsctoolCmd, "-a", "-I", "-M"};
constexpr char kFactoryModeMatchStr[] = "CCD_FLAG_FACTORY_MODE=Y";
constexpr char kInitialFactoryModeMatchStr[] = "INITIAL_FACTORY_MODE=Y";
// Constants for factory mode.
const std::vector<std::string> kEnableFactoryModeArgv{kGsctoolCmd, "-a", "-F",
                                                      "enable"};
const std::vector<std::string> kDisableFactoryModeArgv{kGsctoolCmd, "-a", "-F",
                                                       "disable"};
// Constants for board ID.
const std::vector<std::string> kGetBoardIdArgv{kGsctoolCmd, "-a", "-i", "-M"};
constexpr char kSetBoardIdCmd[] = "/usr/sbin/gsc_set_board_id";
constexpr char kBoardIdTypeRegexp[] = R"(BID_TYPE=([[:xdigit:]]{8}))";
constexpr char kBoardIdFlagsRegexp[] = R"(BID_FLAGS=([[:xdigit:]]{8}))";

// Constants for reboot.
const std::vector<std::string> kRebootArgv{kGsctoolCmd, "-a", "--reboot"};

// Constants for factory config.
const std::vector<std::string> kGetFactoryConfigArgv{kGsctoolCmd, "-a",
                                                     "--factory_config"};
const std::vector<std::string> kSetFactoryConfigArgv{kGsctoolCmd, "-a",
                                                     "--factory_config"};
constexpr char kFactoryConfigRegexp[] = R"(raw value: ([[:xdigit:]]{16}))";

// Constants for CHASSIS_OPEN.
const std::vector<std::string> kGetChassisOpenArgv{kGsctoolCmd, "-a", "-K",
                                                   "chassis_open"};
constexpr char kChassisOpenRegexp[] = R"(Chassis Open: ((true)|(false)))";

// Constants for addressing mode.
constexpr std::array<std::string_view, 3> kAddressingMode = {kGsctoolCmd, "-a",
                                                             "-C"};

// Constants for wpsr.
constexpr std::array<std::string_view, 3> kWpsr = {kGsctoolCmd, "-a", "-E"};

// SPI addressing mode mappings from enum to string.
constexpr char kSpiAddressingMode3Byte[] = "3byte";
constexpr char kSpiAddressingMode4Byte[] = "4byte";
constexpr char kSpiAddressingModeNotProvisioned[] = "Not Provisioned";
constexpr char kSpiAddressingModeUnknown[] = "Unknown";
constexpr auto kSpiAddressingModeMap =
    base::MakeFixedFlatMap<SpiAddressingMode, std::string_view>({
        {SpiAddressingMode::kUnknown, kSpiAddressingModeUnknown},
        {SpiAddressingMode::k3Byte, kSpiAddressingMode3Byte},
        {SpiAddressingMode::k4Byte, kSpiAddressingMode4Byte},
        {SpiAddressingMode::kNotProvisioned, kSpiAddressingModeNotProvisioned},
    });

// Factory config encoding/decoding functions.
// According to b/275356839, factory config is stored in GSC INFO page with 64
// bit length. The lower 5 bits are now allocated to the feature management
// flags.
std::string Uint64ToHexString(uint64_t value) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(16) << value;
  return ss.str();
}

std::string EncodeFactoryConfig(bool is_chassis_branded,
                                int hw_compliance_version) {
  uint64_t factory_config =
      ((is_chassis_branded & 0x1) << 4) | (hw_compliance_version & 0xF);
  return Uint64ToHexString(factory_config);
}

bool DecodeFactoryConfig(const std::string& factory_config_hexstr,
                         bool* is_chassis_branded,
                         int* hw_compliance_version) {
  uint64_t factory_config;
  if (!base::HexStringToUInt64(factory_config_hexstr, &factory_config)) {
    LOG(ERROR) << "Failed to decode hex string " << factory_config_hexstr;
    return false;
  }
  *is_chassis_branded = ((factory_config >> 4) & 0x1);
  *hw_compliance_version = (factory_config & 0xF);
  return true;
}

}  // namespace

GscUtilsImpl::GscUtilsImpl() : GscUtils() {
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
}

GscUtilsImpl::GscUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils)
    : GscUtils(), cmd_utils_(std::move(cmd_utils)) {}

bool GscUtilsImpl::GetRsuChallengeCode(std::string* challenge_code) const {
  // TODO(chenghan): Check with GSC team if we can expose a tpm_managerd API
  //                 for this, so we don't need to depend on `gsctool` output
  //                 format to do extra string parsing.
  std::string output;
  if (!cmd_utils_->GetOutput(kGetRsuChallengeArgv, &output)) {
    LOG(ERROR) << "Failed to get RSU challenge code";
    LOG(ERROR) << output;
    return false;
  }
  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kRsuChallengeRegexp);
  if (!RE2::PartialMatch(string_piece, regexp, challenge_code)) {
    LOG(ERROR) << "Failed to parse RSU challenge code";
    LOG(ERROR) << output;
    return false;
  }
  DLOG(INFO) << "Challenge code: " << *challenge_code;
  return true;
}

bool GscUtilsImpl::PerformRsu(const std::string& unlock_code) const {
  std::vector<std::string> argv(kSendRsuResponseArgv);
  argv.push_back(unlock_code);
  if (std::string output; !cmd_utils_->GetOutput(argv, &output)) {
    DLOG(ERROR) << "RSU failed.";
    DLOG(ERROR) << output;
    return false;
  }
  DLOG(INFO) << "RSU succeeded.";
  return true;
}

bool GscUtilsImpl::EnableFactoryMode() const {
  if (!IsFactoryModeEnabled()) {
    std::string unused_output;
    return cmd_utils_->GetOutput(kEnableFactoryModeArgv, &unused_output);
  }
  return true;
}

bool GscUtilsImpl::DisableFactoryMode() const {
  if (IsFactoryModeEnabled()) {
    std::string unused_output;
    return cmd_utils_->GetOutput(kDisableFactoryModeArgv, &unused_output);
  }
  return true;
}

bool GscUtilsImpl::IsFactoryModeEnabled() const {
  std::string output;
  cmd_utils_->GetOutput(kGetCcdInfoArgv, &output);
  return output.find(kFactoryModeMatchStr) != std::string::npos;
}

bool GscUtilsImpl::IsInitialFactoryModeEnabled() const {
  std::string output;
  cmd_utils_->GetOutput(kGetCcdInfoArgv, &output);
  return output.find(kInitialFactoryModeMatchStr) != std::string::npos;
}

bool GscUtilsImpl::GetBoardIdType(std::string* board_id_type) const {
  std::string output;
  if (!cmd_utils_->GetOutput(kGetBoardIdArgv, &output)) {
    LOG(ERROR) << "Failed to get GSC board ID";
    LOG(ERROR) << output;
    return false;
  }
  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kBoardIdTypeRegexp);
  if (!RE2::PartialMatch(string_piece, regexp, board_id_type)) {
    LOG(ERROR) << "Failed to parse GSC board ID type";
    LOG(ERROR) << output;
    return false;
  }
  return true;
}

bool GscUtilsImpl::GetBoardIdFlags(std::string* board_id_flags) const {
  std::string output;
  if (!cmd_utils_->GetOutput(kGetBoardIdArgv, &output)) {
    LOG(ERROR) << "Failed to get GSC board ID flags";
    LOG(ERROR) << output;
    return false;
  }
  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kBoardIdFlagsRegexp);
  if (!RE2::PartialMatch(string_piece, regexp, board_id_flags)) {
    LOG(ERROR) << "Failed to parse GSC board ID flags";
    LOG(ERROR) << output;
    return false;
  }
  return true;
}

bool GscUtilsImpl::SetBoardId(bool is_custom_label) const {
  std::string output;
  std::vector<std::string> argv{kSetBoardIdCmd};
  if (is_custom_label) {
    argv.push_back("whitelabel_pvt");
  } else {
    argv.push_back("pvt");
  }
  if (!cmd_utils_->GetOutputAndError(argv, &output)) {
    LOG(ERROR) << "Failed to set GSC board ID";
    LOG(ERROR) << output;
    return false;
  }
  return true;
}

bool GscUtilsImpl::Reboot() const {
  std::string unused_output;
  return cmd_utils_->GetOutput(kRebootArgv, &unused_output);
}

bool GscUtilsImpl::GetFactoryConfig(bool* is_chassis_branded,
                                    int* hw_compliance_version) const {
  std::string output;
  if (!cmd_utils_->GetOutput(kGetFactoryConfigArgv, &output)) {
    LOG(ERROR) << "Failed to get factory config";
    LOG(ERROR) << output;
    return false;
  }
  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kFactoryConfigRegexp);
  std::string factory_config_hexstr;
  if (!RE2::PartialMatch(string_piece, regexp, &factory_config_hexstr)) {
    LOG(ERROR) << "Failed to parse factory config";
    LOG(ERROR) << output;
    return false;
  }
  if (!DecodeFactoryConfig(factory_config_hexstr, is_chassis_branded,
                           hw_compliance_version)) {
    LOG(ERROR) << "Failed to parse factory config hex string: "
               << factory_config_hexstr;
    return false;
  }
  return true;
}

bool GscUtilsImpl::SetFactoryConfig(bool is_chassis_branded,
                                    int hw_compliance_version) const {
  std::string factory_config_hexstr =
      EncodeFactoryConfig(is_chassis_branded, hw_compliance_version);
  std::string output;
  std::vector<std::string> argv(kSetFactoryConfigArgv);
  argv.push_back(factory_config_hexstr);
  if (!cmd_utils_->GetOutput(argv, &output)) {
    LOG(ERROR) << "Failed to set factory config";
    LOG(ERROR) << output;
    return false;
  }
  return true;
}

bool GscUtilsImpl::GetChassisOpenStatus(bool* status) {
  std::string output;
  if (!cmd_utils_->GetOutput(kGetChassisOpenArgv, &output)) {
    LOG(ERROR) << "Failed to get CHASSIS_OPEN status";
    LOG(ERROR) << output;
    return false;
  }

  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kChassisOpenRegexp);
  std::string bool_string;
  if (!RE2::PartialMatch(string_piece, regexp, &bool_string)) {
    LOG(ERROR) << "Failed to parse CHASSIS_OPEN status";
    LOG(ERROR) << output;
    return false;
  }

  *status = (bool_string == "true");

  return true;
}

SpiAddressingMode GscUtilsImpl::GetAddressingMode() {
  std::string output;
  std::vector<std::string> argv{kAddressingMode.begin(), kAddressingMode.end()};

  if (!cmd_utils_->GetOutputAndError(argv, &output)) {
    LOG(ERROR) << "Failed to get addressing mode";
    LOG(ERROR) << output;
    return SpiAddressingMode::kUnknown;
  }

  // The output can be "3byte", "4byte", or "not provisioned".
  if (output == "3byte") {
    return SpiAddressingMode::k3Byte;
  } else if (output == "4byte") {
    return SpiAddressingMode::k4Byte;
  } else if (output == "not provisioned") {
    return SpiAddressingMode::kNotProvisioned;
  }

  return SpiAddressingMode::kUnknown;
}

bool GscUtilsImpl::SetAddressingMode(SpiAddressingMode mode) {
  if (mode != SpiAddressingMode::k3Byte && mode != SpiAddressingMode::k4Byte) {
    LOG(ERROR) << "Only 3byte and 4byte addressing modes are available.";
    return false;
  }

  std::string output;
  std::vector<std::string> argv{kAddressingMode.begin(), kAddressingMode.end()};
  argv.push_back(std::string(kSpiAddressingModeMap.at(mode)));

  if (!cmd_utils_->GetOutputAndError(argv, &output)) {
    LOG(ERROR) << "Failed to set addressing mode";
    LOG(ERROR) << output;
    return false;
  }

  return true;
}

SpiAddressingMode GscUtilsImpl::GetAddressingModeByFlashSize(
    uint64_t flash_size) {
  if (flash_size <= 0x1000000) {  // 2^24
    return SpiAddressingMode::k3Byte;
  }
  return SpiAddressingMode::k4Byte;
}

bool GscUtilsImpl::SetWpsr(std::string_view wpsr) {
  std::string output;
  std::vector<std::string> argv{kWpsr.begin(), kWpsr.end()};
  argv.emplace_back(wpsr);

  if (!cmd_utils_->GetOutputAndError(argv, &output)) {
    LOG(ERROR) << "Failed to set wpsr: " << wpsr;
    LOG(ERROR) << output;
    return false;
  }

  return true;
}

std::optional<bool> GscUtilsImpl::IsApWpsrProvisioned() {
  std::string output;
  std::vector<std::string> argv{kWpsr.begin(), kWpsr.end()};

  if (!cmd_utils_->GetOutputAndError(argv, &output)) {
    LOG(ERROR) << "Failed to get wpsr";
    LOG(ERROR) << output;
    return std::nullopt;
  }

  return base::TrimWhitespaceASCII(output, base::TRIM_TRAILING) !=
         "not provisioned";
}

}  // namespace rmad
