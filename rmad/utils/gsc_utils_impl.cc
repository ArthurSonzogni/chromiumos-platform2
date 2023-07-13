// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/gsc_utils_impl.h>

#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "rmad/utils/cmd_utils_impl.h"

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
constexpr char kSetBoardIdCmd[] = "/usr/share/cros/cr50-set-board-id.sh";
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
  if (!cmd_utils_->GetOutput(argv, &output)) {
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

}  // namespace rmad
