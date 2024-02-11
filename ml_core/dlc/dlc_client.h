// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_CORE_DLC_DLC_CLIENT_H_
#define ML_CORE_DLC_DLC_CLIENT_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/functional/callback.h>

namespace cros {

class DlcClient {
 public:
  // Factory function for creating DlcClients.
  static std::unique_ptr<DlcClient> Create(
      base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
      base::OnceCallback<void(const std::string&)> error_cb);
  virtual ~DlcClient() = default;

  // Asks DLC Service to start installing the ML Core DLC. Retries a limited
  // number of times if DLC Service is busy.
  virtual void InstallDlc() = 0;

  // Causes UMA histograms for this object to be emitted, with the specified
  // base name. Emitted histograms are named as follows:
  // {metrics_base_name}.MlCore.{specific histogram name}
  // If this function is not called before InstallDlc(), histograms will not be
  // emitted.
  virtual void SetMetricsBaseName(const std::string& metrics_base_name) = 0;

  // For Unit Tests, allow using a fixed path instead of DLC, eg
  // /build/share/ml_core
  static void SetDlcPathForTest(const base::FilePath* path);
};

}  // namespace cros

#endif  // ML_CORE_DLC_DLC_CLIENT_H_
