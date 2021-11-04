// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_cryptohome_client.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "rmad/constants.h"

namespace rmad {
namespace fake {

FakeCryptohomeClient::FakeCryptohomeClient(
    const base::FilePath& working_dir_path)
    : CryptohomeClient(), working_dir_path_(working_dir_path) {}

bool FakeCryptohomeClient::HasFwmp() {
  return IsEnrolled();
}

bool FakeCryptohomeClient::IsEnrolled() {
  const base::FilePath is_enrolled_path =
      working_dir_path_.AppendASCII(kIsEnrolledFilePath);
  return base::PathExists(is_enrolled_path);
}

}  // namespace fake
}  // namespace rmad
