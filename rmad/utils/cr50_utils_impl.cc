// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/cr50_utils_impl.h>

#include <string>
#include <vector>

#include <base/process/launch.h>
#include <base/strings/string_util.h>

namespace rmad {

constexpr char kGsctoolCmd[] = "gsctool";

bool Cr50UtilsImpl::RoVerificationKeyPressed() const {
  // TODO(b/181000999): Send a D-Bus query to tpm_managerd when API is ready.
  return false;
}

bool Cr50UtilsImpl::GetRsuChallenge(std::string* challenge) const {
  // TODO(chenghan): Check with cr50 team if we can expose a tpm_managerd API
  //                 for this, so we don't need to depend on `gsctool` output
  //                 format to do extra string parsing.
  static const std::vector<std::string> argv{kGsctoolCmd, "-a", "-r"};
  if (base::GetAppOutput(argv, challenge)) {
    base::RemoveChars(*challenge, base::kWhitespaceASCII, challenge);
    base::ReplaceFirstSubstringAfterOffset(challenge, 0, "Challenge:", "");
    return true;
  }
  return false;
}

}  // namespace rmad
