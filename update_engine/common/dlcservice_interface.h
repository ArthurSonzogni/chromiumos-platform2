// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_DLCSERVICE_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_DLCSERVICE_INTERFACE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <libimageloader/manifest.h>

namespace chromeos_update_engine {

// The abstract dlcservice interface defines the interaction with the
// platform's dlcservice.
class DlcServiceInterface {
 public:
  DlcServiceInterface(const DlcServiceInterface&) = delete;
  DlcServiceInterface& operator=(const DlcServiceInterface&) = delete;

  virtual ~DlcServiceInterface() = default;

  // Returns true and a list of installed DLC ids in |dlc_ids|.
  // On failure it returns false.
  virtual bool GetDlcsToUpdate(std::vector<std::string>* dlc_ids) = 0;

  // Returns true if dlcservice successfully handled the install completion
  // method call, otherwise false.
  virtual bool InstallCompleted(const std::vector<std::string>& dlc_ids) = 0;

  // Returns true if dlcservice successfully handled the update completion
  // method call, otherwise false.
  virtual bool UpdateCompleted(const std::vector<std::string>& dlc_ids) = 0;

 protected:
  DlcServiceInterface() = default;
};

class DlcUtilsInterface {
 public:
  DlcUtilsInterface(const DlcUtilsInterface&) = delete;
  DlcUtilsInterface& operator=(const DlcUtilsInterface&) = delete;

  virtual ~DlcUtilsInterface() = default;

  virtual std::shared_ptr<imageloader::Manifest> GetDlcManifest(
      const std::string& id, const base::FilePath& dlc_manifest_path) = 0;

 protected:
  DlcUtilsInterface() = default;
};

// This factory function creates a new DlcServiceInterface instance for the
// current platform.
std::unique_ptr<DlcServiceInterface> CreateDlcService();

// This factory function creates a new DlcUtilsInterface instance for the
// current platform.
std::unique_ptr<DlcUtilsInterface> CreateDlcUtils();

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_DLCSERVICE_INTERFACE_H_
