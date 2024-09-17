// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_EC_FIRMWARE_H_
#define LIBEC_EC_FIRMWARE_H_

#include <fmap.h>

#include <memory>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/memory_mapped_file.h>
#include <chromeos/ec/ec_commands.h>

namespace ec {

class EcFirmware {
 public:
  static std::unique_ptr<EcFirmware> Create(const base::FilePath file);
  ~EcFirmware() = default;

  base::span<const uint8_t> GetData(const enum ec_image image) const;
  std::optional<uint32_t> GetOffset(const enum ec_image image) const;
  std::optional<uint32_t> GetSize(const enum ec_image image) const;
  std::optional<std::string> GetVersion(const enum ec_image image) const;

 private:
  EcFirmware() = default;
  bool VerifyImage(enum ec_image image) const;
  base::MemoryMappedFile image_;
  const struct fmap* fmap_ = nullptr;
};

}  // namespace ec

#endif  // LIBEC_EC_FIRMWARE_H_
