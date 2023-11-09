// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_UTILS_UTILS_INTERFACE_H_
#define DLCSERVICE_UTILS_UTILS_INTERFACE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libimageloader/manifest.h>

#include "dlcservice/types.h"

namespace dlcservice {

enum class BRILLO_EXPORT PartitionSlot {
  A,
  B,
};

class BRILLO_EXPORT UtilsInterface {
 public:
  UtilsInterface() = default;
  virtual ~UtilsInterface() = default;

  UtilsInterface(const UtilsInterface&) = delete;
  UtilsInterface& operator=(const UtilsInterface&) = delete;

  // Takes a DLC ID and returns the logical volume name based on slot.
  virtual std::string LogicalVolumeName(const std::string& id,
                                        PartitionSlot slot) = 0;

  // Returns the DLC ID based off of logical volume name, returns empty when an
  // invalid DLC logical volume name is given.
  virtual std::string LogicalVolumeNameToId(const std::string& lv_name) = 0;

  // Hashes the file at |path|.
  // Pass zero or less for `size` to skip size check.
  virtual bool HashFile(const base::FilePath& path,
                        int64_t size,
                        std::vector<uint8_t>* sha256,
                        bool skip_size_check = false) = 0;

  // Retrieves the given DLC (id + package)  manifest.
  virtual std::shared_ptr<imageloader::Manifest> GetDlcManifest(
      const base::FilePath& dlc_manifest_path,
      const std::string& id,
      const std::string& package) = 0;

  // Retrieves the given DLC (id) manifest from metadata.
  virtual std::shared_ptr<imageloader::Manifest> GetDlcManifest(
      const std::string& id, const base::FilePath& dlc_manifest_path) = 0;

  virtual DlcIdList GetSupportedDlcIds(const base::FilePath& metadata_path) = 0;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_UTILS_UTILS_INTERFACE_H_
