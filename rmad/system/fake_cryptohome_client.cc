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

bool FakeCryptohomeClient::IsCcdBlocked() {
  const base::FilePath block_ccd_file_path =
      working_dir_path_.AppendASCII(kBlockCcdFilePath);
  return base::PathExists(block_ccd_file_path);
}

}  // namespace fake
}  // namespace rmad
