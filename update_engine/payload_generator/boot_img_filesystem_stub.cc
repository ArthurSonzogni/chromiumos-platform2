// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/boot_img_filesystem.h"

namespace chromeos_update_engine {
std::unique_ptr<BootImgFilesystem> BootImgFilesystem::CreateFromFile(
    const std::string& /* filename */) {
  return nullptr;
}

size_t BootImgFilesystem::GetBlockSize() const {
  return 4096;
}

size_t BootImgFilesystem::GetBlockCount() const {
  return 0;
}

FilesystemInterface::File BootImgFilesystem::GetFile(
    const std::string& /* name */,
    uint64_t /* offset */,
    uint64_t /* size */) const {
  return {};
}

bool BootImgFilesystem::GetFiles(std::vector<File>* /* files */) const {
  return false;
}

bool BootImgFilesystem::LoadSettings(brillo::KeyValueStore* /* store */) const {
  return false;
}

}  // namespace chromeos_update_engine
