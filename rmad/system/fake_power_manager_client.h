// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_FAKE_POWER_MANAGER_CLIENT_H_
#define RMAD_SYSTEM_FAKE_POWER_MANAGER_CLIENT_H_

#include "rmad/system/power_manager_client.h"

#include <base/files/file_path.h>

namespace rmad {
namespace fake {

class FakePowerManagerClient : public PowerManagerClient {
 public:
  explicit FakePowerManagerClient(const base::FilePath& working_dir_path);
  FakePowerManagerClient(const FakePowerManagerClient&) = delete;
  FakePowerManagerClient& operator=(const FakePowerManagerClient&) = delete;
  ~FakePowerManagerClient() override = default;

  bool Restart() override;
  bool Shutdown() override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_SYSTEM_FAKE_POWER_MANAGER_CLIENT_H_
