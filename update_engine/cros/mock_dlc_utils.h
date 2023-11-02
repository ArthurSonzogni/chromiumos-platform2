//
// Copyright (C) 2023 The Android Open Source Project
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
