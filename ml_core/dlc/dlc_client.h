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
  // Check the returned pointer for `nullptr` to determine success.
  // The callbacks will not be invoked if DlcClient fails to initialize.
  static std::unique_ptr<DlcClient> Create(
      const std::string& dlc_id,
      base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
      base::OnceCallback<void(const std::string&)> error_cb);

  // Not thread-safe; must be destroyed in the same sequence as it was created.
  virtual ~DlcClient() = default;

  // Asks DLC Service to start installing the DLC. Retries a limited
  // number of times if DLC Service is busy.
  // Invokes registered callbacks on completion (success or failure).
  // Subsequent calls after completion will restart installation without
  // triggering callbacks.
  // Thread-safe; can be called from any sequence.
  virtual void InstallDlc() = 0;

  // Causes UMA histograms for this object to be emitted, with the specified
  // base name. Emitted histograms are named as follows:
  // {metrics_base_name}.{specific histogram name}
  // If this function is not called before InstallDlc(), histograms will not be
  // emitted.
  virtual void SetMetricsBaseName(const std::string& metrics_base_name) = 0;

  // For Unit Tests and local development. Allows using a fixed path instead of
  // DLC (e.g., /build/share/ml_core, /usr/local/lib64). This should be called
  // before creating DlcClient. When this is set, all the following DLCs
  // downloaded by the current package will use this path.
  // Can be reset by setting a nullptr.
  static void SetDlcPathForTest(const base::FilePath* path);
};

}  // namespace cros

#endif  // ML_CORE_DLC_DLC_CLIENT_H_
