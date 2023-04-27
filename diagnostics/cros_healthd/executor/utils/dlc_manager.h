// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_DLC_MANAGER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_DLC_MANAGER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/functional/callback_helpers.h>
#include <brillo/errors/error.h>

namespace dlcservice {
class DlcState;
};  // namespace dlcservice

namespace org::chromium {
class DlcServiceInterfaceProxyInterface;
}  // namespace org::chromium

namespace diagnostics {

// Interface for accessing verifed DLC and getting DLC root mount path.
class DlcManager {
 public:
  explicit DlcManager(
      org::chromium::DlcServiceInterfaceProxyInterface* dlcservice_proxy);
  DlcManager(const DlcManager&) = delete;
  DlcManager& operator=(const DlcManager&) = delete;
  virtual ~DlcManager() = default;

  // Check the DLC state and get its root path. Installation will be triggered
  // if the DLC is unexpectedly missing.
  virtual void GetBinaryRootPath(
      const std::string& dlc_id,
      base::OnceCallback<void(std::optional<std::string>)> root_path_cb) const;

 private:
  // Handle the response of service availability.
  void HandleDlcServiceAvailableResponse(
      const std::string& dlc_id,
      base::OnceCallback<void(std::optional<std::string>)> root_path_cb,
      bool service_is_available) const;

  // Handle the response of installing DLC.
  void HandleDlcInstallResponse(
      const std::string& dlc_id,
      base::OnceCallback<void(std::optional<std::string>)> root_path_cb,
      brillo::Error* err) const;

  // Handle the response of DLC state.
  void HandleDlcStateResponse(
      const std::string& dlc_id,
      base::OnceCallback<void(std::optional<std::string>)> root_path_cb,
      brillo::Error* err,
      const dlcservice::DlcState& state) const;

  // Unowned pointer that should outlive this instance.
  org::chromium::DlcServiceInterfaceProxyInterface* const dlcservice_proxy_;

  // Must be the last member of the class.
  base::WeakPtrFactory<DlcManager> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_DLC_MANAGER_H_
