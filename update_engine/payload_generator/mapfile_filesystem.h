// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A filesystem parser based on the Android .map files. When generating a
// filesystem with the Android tools, either squashfs or ext4, a .map file can
// be generated at the same time with the list of files and the 4K-blocks where
// the data for those files is located in the filesystem. This class parses this
// .map text file instead of parsing the structure of the actual filesystem
// contents.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_MAPFILE_FILESYSTEM_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_MAPFILE_FILESYSTEM_H_

#include "update_engine/payload_generator/filesystem_interface.h"

#include <memory>
#include <string>
#include <vector>

namespace chromeos_update_engine {

class MapfileFilesystem : public FilesystemInterface {
 public:
  static std::unique_ptr<MapfileFilesystem> CreateFromFile(
      const std::string& filename, const std::string& mapfile_filename);
  MapfileFilesystem(const MapfileFilesystem&) = delete;
  MapfileFilesystem& operator=(const MapfileFilesystem&) = delete;
  virtual ~MapfileFilesystem() = default;

  // FilesystemInterface overrides.
  size_t GetBlockSize() const override;
  size_t GetBlockCount() const override;

  // All the generated FilesystemInterface::File are reported as regular files.
  // Files may overlap with other files in the same block.
  bool GetFiles(std::vector<File>* files) const override;

  bool LoadSettings(brillo::KeyValueStore* store) const override;

 private:
  MapfileFilesystem(const std::string& mapfile_filename, off_t num_blocks);

  // The file where the map filesystem is stored.
  std::string mapfile_filename_;

  // The number of blocks in the filesystem.
  off_t num_blocks_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_MAPFILE_FILESYSTEM_H_
