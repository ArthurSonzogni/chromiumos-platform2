// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_DLCSERVICE_STUB_H_
#define UPDATE_ENGINE_COMMON_DLCSERVICE_STUB_H_

#include <memory>
#include <string>
#include <vector>

#include "update_engine/common/dlcservice_interface.h"

namespace chromeos_update_engine {

// An implementation of the DlcServiceInterface that does nothing.
class DlcServiceStub : public DlcServiceInterface {
 public:
  DlcServiceStub() = default;
  DlcServiceStub(const DlcServiceStub&) = delete;
  DlcServiceStub& operator=(const DlcServiceStub&) = delete;

  ~DlcServiceStub() = default;

  // BootControlInterface overrides.
  bool GetDlcsToUpdate(std::vector<std::string>* dlc_ids) override;
  bool InstallCompleted(const std::vector<std::string>& dlc_ids) override;
  bool UpdateCompleted(const std::vector<std::string>& dlc_ids) override;
};

class DlcUtilsStub : public DlcUtilsInterface {
 public:
  DlcUtilsStub() = default;
  DlcUtilsStub(const DlcUtilsStub&) = delete;
  DlcUtilsStub& operator=(const DlcUtilsStub&) = delete;

  ~DlcUtilsStub() = default;

  std::shared_ptr<imageloader::Manifest> GetDlcManifest(
      const std::string& id, const base::FilePath& dlc_manifest_path) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_DLCSERVICE_STUB_H_
