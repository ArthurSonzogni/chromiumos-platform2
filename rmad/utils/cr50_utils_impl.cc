// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/cr50_utils_impl.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "rmad/utils/cmd_utils_impl.h"

namespace rmad {

namespace {

constexpr char kGsctoolCmd[] = "gsctool";
constexpr char kFactoryModeMatchStr[] = "Capabilities are modified.";
const std::vector<std::string> kRsuArgv{kGsctoolCmd, "-a", "-r"};
const std::vector<std::string> kCcdInfoArgv{kGsctoolCmd, "-a", "-I"};
const std::vector<std::string> kEnableFactoryModeArgv{kGsctoolCmd, "-a", "-F",
                                                      "enable"};
const std::vector<std::string> kDisableFactoryModeArgv{kGsctoolCmd, "-a", "-F",
                                                       "disable"};
const std::vector<std::string> kGetBoardIdArgv{kGsctoolCmd, "-a", "-i", "-M"};

constexpr char kSetBoardIdCmd[] = "/usr/share/cros/cr50-set-board-id.sh";
constexpr char kBoardIdTypeRegexp[] = R"(BID_TYPE=([[:xdigit:]]{8}))";
constexpr char kBoardIdFlagsRegexp[] = R"(BID_FLAGS=([[:xdigit:]]{8}))";

}  // namespace

Cr50UtilsImpl::Cr50UtilsImpl() : Cr50Utils() {
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
}

Cr50UtilsImpl::Cr50UtilsImpl(std::unique_ptr<CmdUtils> cmd_utils)
    : Cr50Utils(), cmd_utils_(std::move(cmd_utils)) {}

bool Cr50UtilsImpl::GetRsuChallengeCode(std::string* challenge_code) const {
  // TODO(chenghan): Check with cr50 team if we can expose a tpm_managerd API
  //                 for this, so we don't need to depend on `gsctool` output
  //                 format to do extra string parsing.
  if (cmd_utils_->GetOutput(kRsuArgv, challenge_code)) {
    base::RemoveChars(*challenge_code, base::kWhitespaceASCII, challenge_code);
    base::ReplaceFirstSubstringAfterOffset(challenge_code, 0, "Challenge:", "");
    LOG(INFO) << "Challenge code: " << *challenge_code;
    return true;
  }
  return false;
}

bool Cr50UtilsImpl::PerformRsu(const std::string& unlock_code) const {
  std::vector<std::string> argv(kRsuArgv);
  argv.push_back(unlock_code);
  std::string output;
  if (cmd_utils_->GetOutput(argv, &output)) {
    LOG(INFO) << "RSU succeeded.";
    return true;
  }
  LOG(INFO) << "RSU failed.";
  LOG(ERROR) << output;
  return false;
}

bool Cr50UtilsImpl::EnableFactoryMode() const {
  if (!IsFactoryModeEnabled()) {
    std::string unused_output;
    return cmd_utils_->GetOutput(kEnableFactoryModeArgv, &unused_output);
  }
  return true;
}

bool Cr50UtilsImpl::DisableFactoryMode() const {
  if (IsFactoryModeEnabled()) {
    std::string unused_output;
    return cmd_utils_->GetOutput(kDisableFactoryModeArgv, &unused_output);
  }
  return true;
}

bool Cr50UtilsImpl::IsFactoryModeEnabled() const {
  std::string output;
  cmd_utils_->GetOutput(kCcdInfoArgv, &output);
  return output.find(kFactoryModeMatchStr) != std::string::npos;
}

bool Cr50UtilsImpl::GetBoardIdType(std::string* board_id_type) const {
  std::string output;
  if (!cmd_utils_->GetOutput(kGetBoardIdArgv, &output)) {
    LOG(ERROR) << "Failed to get cr50 board ID";
    LOG(ERROR) << output;
    return false;
  }
  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kBoardIdTypeRegexp);
  if (!RE2::PartialMatch(string_piece, regexp, board_id_type)) {
    LOG(ERROR) << "Failed to parse cr50 board ID type";
    LOG(ERROR) << output;
    return false;
  }
  return true;
}

bool Cr50UtilsImpl::GetBoardIdFlags(std::string* board_id_flags) const {
  std::string output;
  if (!cmd_utils_->GetOutput(kGetBoardIdArgv, &output)) {
    LOG(ERROR) << "Failed to get cr50 board ID flags";
    LOG(ERROR) << output;
    return false;
  }
  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kBoardIdFlagsRegexp);
  if (!RE2::PartialMatch(string_piece, regexp, board_id_flags)) {
    LOG(ERROR) << "Failed to parse cr50 board ID flags";
    LOG(ERROR) << output;
    return false;
  }
  return true;
}

bool Cr50UtilsImpl::SetBoardId(bool is_custom_label) const {
  std::string output;
  std::vector<std::string> argv{kSetBoardIdCmd};
  if (is_custom_label) {
    argv.push_back("whitelabel_pvt");
  } else {
    argv.push_back("pvt");
  }
  if (!cmd_utils_->GetOutput(argv, &output)) {
    LOG(ERROR) << "Failed to set cr50 board ID";
    LOG(ERROR) << output;
    return false;
  }
  return true;
}

}  // namespace rmad
