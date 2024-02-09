// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_BOOT_IMG_FILESYSTEM_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_BOOT_IMG_FILESYSTEM_H_

#include "update_engine/payload_generator/filesystem_interface.h"

#include <memory>
#include <string>
#include <vector>

namespace chromeos_update_engine {

class BootImgFilesystem : public FilesystemInterface {
 public:
  // Creates an BootImgFilesystem from an Android boot.img file.
  static std::unique_ptr<BootImgFilesystem> CreateFromFile(
      const std::string& filename);

  BootImgFilesystem(const BootImgFilesystem&) = delete;
  BootImgFilesystem& operator=(const BootImgFilesystem&) = delete;

  ~BootImgFilesystem() override = default;

  // FilesystemInterface overrides.
  size_t GetBlockSize() const override;
  size_t GetBlockCount() const override;

  // GetFiles will return one FilesystemInterface::File for kernel and one for
  // ramdisk.
  bool GetFiles(std::vector<File>* files) const override;

  bool LoadSettings(brillo::KeyValueStore* store) const override;

 private:
  friend class BootImgFilesystemTest;

  BootImgFilesystem() = default;

  File GetFile(const std::string& name, uint64_t offset, uint64_t size) const;

  // The boot.img file path.
  std::string filename_;

  uint32_t kernel_size_;  /* size in bytes */
  uint32_t ramdisk_size_; /* size in bytes */
  uint32_t page_size_;    /* flash page size we assume */
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_BOOT_IMG_FILESYSTEM_H_
