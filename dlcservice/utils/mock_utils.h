// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_UTILS_MOCK_UTILS_H_
#define DLCSERVICE_UTILS_MOCK_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libimageloader/manifest.h>

#include "dlcservice/utils/utils_interface.h"

namespace dlcservice {

class MockUtils : public UtilsInterface {
 public:
  MockUtils() = default;

  MockUtils(const MockUtils&) = delete;
  MockUtils& operator=(const MockUtils&) = delete;

  MOCK_METHOD(std::string,
              LogicalVolumeName,
              (const std::string& id, PartitionSlot slot),
              (override));
  MOCK_METHOD(bool,
              HashFile,
              (const base::FilePath& path,
               int64_t size,
               std::vector<uint8_t>* sha256,
               bool skip_size_check),
              (override));
  MOCK_METHOD(std::shared_ptr<imageloader::Manifest>,
              GetDlcManifest,
              (const base::FilePath& dlc_manifest_path,
               const std::string& id,
               const std::string& package),
              (override));
};

}  // namespace dlcservice

#endif  // DLCSERVICE_UTILS_MOCK_UTILS_H_
