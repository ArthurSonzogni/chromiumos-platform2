// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_tpm_manager_client.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "rmad/constants.h"

namespace {

bool RoVerificationStatus_Parse(const std::string& str,
                                rmad::RoVerificationStatus* status) {
  if (str == "NOT_TRIGGERED") {
    *status = rmad::RoVerificationStatus::NOT_TRIGGERED;
    return true;
  } else if (str == "PASS") {
    *status = rmad::RoVerificationStatus::PASS;
    return true;
  } else if (str == "FAIL") {
    *status = rmad::RoVerificationStatus::FAIL;
    return true;
  } else if (str == "UNSUPPORTED") {
    *status = rmad::RoVerificationStatus::UNSUPPORTED;
    return true;
  } else {
    return false;
  }
}

}  // namespace

namespace rmad {
namespace fake {

FakeTpmManagerClient::FakeTpmManagerClient(
    const base::FilePath& working_dir_path)
    : TpmManagerClient(), working_dir_path_(working_dir_path) {}

bool FakeTpmManagerClient::GetRoVerificationStatus(
    RoVerificationStatus* ro_verification_status) {
  base::FilePath status_path =
      working_dir_path_.AppendASCII(kRoVerificationStatusFilePath);
  if (std::string status_str;
      base::PathExists(status_path) &&
      base::ReadFileToString(status_path, &status_str)) {
    VLOG(1) << "Found injected RO verification result";
    base::TrimWhitespaceASCII(status_str, base::TRIM_ALL, &status_str);
    return RoVerificationStatus_Parse(status_str, ro_verification_status);
  } else {
    return false;
  }
}

}  // namespace fake
}  // namespace rmad
