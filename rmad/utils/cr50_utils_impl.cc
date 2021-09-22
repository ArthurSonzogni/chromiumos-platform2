// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/cr50_utils_impl.h>

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_util.h>

namespace rmad {

namespace {

constexpr char kGsctoolCmd[] = "gsctool";
constexpr char kFactoryModeMatchStr[] = "Capabilities are modified.";
const std::vector<std::string> kRsuArgv{kGsctoolCmd, "-a", "-r"};
const std::vector<std::string> kCcdInfoArgv{kGsctoolCmd, "-a", "-I"};
const std::vector<std::string> kEnableFactoryModeArgv{kGsctoolCmd, "-a", "-F",
                                                      "enable"};

}  // namespace

bool Cr50UtilsImpl::GetRsuChallengeCode(std::string* challenge_code) const {
  // TODO(chenghan): Check with cr50 team if we can expose a tpm_managerd API
  //                 for this, so we don't need to depend on `gsctool` output
  //                 format to do extra string parsing.
  if (base::GetAppOutput(kRsuArgv, challenge_code)) {
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
  if (base::GetAppOutput(argv, &output)) {
    LOG(INFO) << "RSU succeeded.";
    return true;
  }
  LOG(INFO) << "RSU failed.";
  LOG(ERROR) << output;
  return false;
}

bool Cr50UtilsImpl::EnableFactoryMode() const {
  if (!IsFactoryModeEnabled()) {
    return base::GetAppOutput(kEnableFactoryModeArgv, nullptr);
  }
  return true;
}

bool Cr50UtilsImpl::IsFactoryModeEnabled() const {
  std::string output;
  base::GetAppOutput(kCcdInfoArgv, &output);
  return output.find(kFactoryModeMatchStr) != std::string::npos;
}

}  // namespace rmad
