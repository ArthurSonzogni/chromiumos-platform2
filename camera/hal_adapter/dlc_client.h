// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_HAL_ADAPTER_DLC_CLIENT_H_
#define CAMERA_HAL_ADAPTER_DLC_CLIENT_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>

namespace cros {

class DlcClient {
 public:
  static std::unique_ptr<DlcClient> Create(
      base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
      base::OnceCallback<void(const std::string&)> error_cb);
  virtual void InstallDlc() = 0;
  virtual ~DlcClient() = default;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_DLC_CLIENT_H_
