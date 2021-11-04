// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_FAKE_CRYPTOHOME_CLIENT_H_
#define RMAD_SYSTEM_FAKE_CRYPTOHOME_CLIENT_H_

#include "rmad/system/cryptohome_client.h"

#include <base/files/file_path.h>

namespace rmad {
namespace fake {

class FakeCryptohomeClient : public CryptohomeClient {
 public:
  explicit FakeCryptohomeClient(const base::FilePath& working_dir_path);
  FakeCryptohomeClient(const FakeCryptohomeClient&) = delete;
  FakeCryptohomeClient& operator=(const FakeCryptohomeClient&) = delete;
  ~FakeCryptohomeClient() override = default;

  bool HasFwmp() override;
  bool IsEnrolled() override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_SYSTEM_FAKE_CRYPTOHOME_CLIENT_H_
