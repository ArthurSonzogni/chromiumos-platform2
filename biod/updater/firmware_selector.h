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

class FirmwareSelectorInterface {
 public:
  virtual ~FirmwareSelectorInterface() = default;
  virtual bool IsBetaFirmwareAllowed() const = 0;
  virtual void AllowBetaFirmware(bool enable) = 0;

  enum class FindFirmwareFileStatus {
    kNoDirectory,
    kFileNotFound,
    kMultipleFiles,
  };

  virtual base::expected<base::FilePath, FindFirmwareFileStatus>
  FindFirmwareFile(const std::string& board_name) = 0;
  static std::string FindFirmwareFileStatusToString(
      FindFirmwareFileStatus status);
};

class FirmwareSelector : public FirmwareSelectorInterface {
 public:
  FirmwareSelector(base::FilePath base_path, base::FilePath firmware_dir)
      : base_path_(base_path), firmware_dir_(firmware_dir) {}
  ~FirmwareSelector() override = default;
  bool IsBetaFirmwareAllowed() const override;
  void AllowBetaFirmware(bool enable) override;
  base::expected<base::FilePath, FindFirmwareFileStatus> FindFirmwareFile(
      const std::string& board_name) override;

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
