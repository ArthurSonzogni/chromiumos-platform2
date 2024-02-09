// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_DLCSERVICE_CHROMEOS_H_
#define UPDATE_ENGINE_CROS_DLCSERVICE_CHROMEOS_H_

#include <memory>
#include <string>
#include <vector>

#include <libdlcservice/utils.h>

#include "update_engine/common/dlcservice_interface.h"

namespace chromeos_update_engine {

// The Chrome OS implementation of the DlcServiceInterface. This interface
// interacts with dlcservice via D-Bus.
class DlcServiceChromeOS : public DlcServiceInterface {
 public:
  DlcServiceChromeOS() = default;
  DlcServiceChromeOS(const DlcServiceChromeOS&) = delete;
  DlcServiceChromeOS& operator=(const DlcServiceChromeOS&) = delete;

  ~DlcServiceChromeOS() = default;

  // DlcServiceInterface overrides.

  // Will clear the |dlc_ids|, passed to be modified. Clearing by default has
  // the added benefit of avoiding indeterminate behavior in the case that
  // |dlc_ids| wasn't empty to begin which would lead to possible duplicates and
  // cases when error was not checked it's still safe.
  bool GetDlcsToUpdate(std::vector<std::string>* dlc_ids) override;

  // Call into dlcservice for it to mark the DLC IDs as being installed.
  bool InstallCompleted(const std::vector<std::string>& dlc_ids) override;

  // Call into dlcservice for it to mark the DLC IDs as being updated.
  bool UpdateCompleted(const std::vector<std::string>& dlc_ids) override;
};

class DlcUtilsChromeOS : public DlcUtilsInterface {
 public:
  DlcUtilsChromeOS() = default;
  DlcUtilsChromeOS(const DlcUtilsChromeOS&) = delete;
  DlcUtilsChromeOS& operator=(const DlcUtilsChromeOS&) = delete;

  ~DlcUtilsChromeOS() = default;

  std::shared_ptr<imageloader::Manifest> GetDlcManifest(
      const std::string& id, const base::FilePath& dlc_manifest_path) override;

 private:
  dlcservice::Utils utils_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_DLCSERVICE_CHROMEOS_H_
