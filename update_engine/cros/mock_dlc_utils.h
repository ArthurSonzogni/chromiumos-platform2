// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_MOCK_DLC_UTILS_H_
#define UPDATE_ENGINE_CROS_MOCK_DLC_UTILS_H_

#include <memory>
#include <string>

#include <gmock/gmock.h>

#include "update_engine/common/dlcservice_interface.h"

namespace chromeos_update_engine {

class MockDlcUtils : public DlcUtilsInterface {
 public:
  MockDlcUtils() = default;
  MockDlcUtils(const MockDlcUtils&) = delete;
  MockDlcUtils& operator=(const MockDlcUtils&) = delete;

  ~MockDlcUtils() = default;

  MOCK_METHOD(std::shared_ptr<imageloader::Manifest>,
              GetDlcManifest,
              (const std::string& id, const base::FilePath& dlc_manifest_path),
              (override));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_DLCSERVICE_CHROMEOS_H_
