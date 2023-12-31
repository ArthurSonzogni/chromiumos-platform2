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

#include "update_engine/common/dlcservice_stub.h"

#include <memory>

using std::string;
using std::vector;

namespace chromeos_update_engine {

std::unique_ptr<DlcServiceInterface> CreateDlcService() {
  return std::make_unique<DlcServiceStub>();
}

std::unique_ptr<DlcUtilsInterface> CreateDlcUtils() {
  return std::make_unique<DlcUtilsStub>();
}

bool DlcServiceStub::GetDlcsToUpdate(vector<string>* dlc_ids) {
  if (dlc_ids)
    dlc_ids->clear();
  return true;
}

bool DlcServiceStub::InstallCompleted(const vector<string>& dlc_ids) {
  return true;
}
bool DlcServiceStub::UpdateCompleted(const vector<string>& dlc_ids) {
  return true;
}

std::shared_ptr<imageloader::Manifest> DlcUtilsStub::GetDlcManifest(
    const std::string& id, const base::FilePath& dlc_manifest_path) {
  return nullptr;
}

}  // namespace chromeos_update_engine
