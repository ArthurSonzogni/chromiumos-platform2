// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
