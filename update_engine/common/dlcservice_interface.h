//
// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

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
