// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UPDATER_FIRMWARE_SELECTOR_H_
#define BIOD_UPDATER_FIRMWARE_SELECTOR_H_

#include <base/files/file_path.h>

namespace biod {

class FirmwareSelector {
 public:
  explicit FirmwareSelector(base::FilePath base_path) : base_path_(base_path) {}
  virtual ~FirmwareSelector() = default;
  virtual base::FilePath GetFirmwarePath() const;

 private:
  const base::FilePath base_path_;
};

}  // namespace biod

#endif  // BIOD_UPDATER_FIRMWARE_SELECTOR_H_
