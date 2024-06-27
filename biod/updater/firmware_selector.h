// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UPDATER_FIRMWARE_SELECTOR_H_
#define BIOD_UPDATER_FIRMWARE_SELECTOR_H_

#include <string>

#include <base/files/file_path.h>
#include <base/types/expected.h>

namespace biod {
namespace updater {

class FirmwareSelector {
 public:
  FirmwareSelector(base::FilePath base_path, base::FilePath firmware_dir)
      : base_path_(base_path), firmware_dir_(firmware_dir) {}
  virtual ~FirmwareSelector() = default;
  virtual bool IsBetaFirmwareAllowed() const;
  virtual void AllowBetaFirmware(bool enable);

  enum class FindFirmwareFileStatus {
    kNoDirectory,
    kFileNotFound,
    kMultipleFiles,
  };

  virtual base::expected<base::FilePath, FindFirmwareFileStatus>
  FindFirmwareFile(const std::string& board_name);
  static std::string FindFirmwareFileStatusToString(
      FindFirmwareFileStatus status);

 private:
  // Searches for the externally packaged firmware binary using a glob.
  // The returned firmware has not been validated.
  base::expected<base::FilePath, FindFirmwareFileStatus> FindFirmwareFileAtDir(
      const base::FilePath& directory, const std::string& board_name);
  const base::FilePath base_path_;
  const base::FilePath firmware_dir_;
};

}  // namespace updater
}  // namespace biod

#endif  // BIOD_UPDATER_FIRMWARE_SELECTOR_H_
