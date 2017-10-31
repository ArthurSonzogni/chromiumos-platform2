// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef IMAGELOADER_IMAGELOADER_IMPL_H_
#define IMAGELOADER_IMAGELOADER_IMPL_H_

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/gtest_prod_util.h>
#include <base/macros.h>

#include "helper_process.h"

namespace imageloader {

using Keys = std::vector<std::vector<uint8_t>>;

struct ImageLoaderConfig {
  ImageLoaderConfig(const Keys& keys, const char* storage_path,
                    const char* mount_path)
      : keys(keys),
        storage_dir(storage_path),
        mount_path(mount_path) {}

  Keys keys;
  base::FilePath storage_dir;
  base::FilePath mount_path;
};

class ImageLoaderImpl {
 public:
  // Instantiate an object with a configuration object.
  explicit ImageLoaderImpl(ImageLoaderConfig config)
      : config_(std::move(config)) {}

  // Register a component.
  bool RegisterComponent(const std::string& name, const std::string& version,
                         const std::string& component_folder_abs_path);

  bool RemoveComponent(const std::string& name);

  // Get component version given component name.
  std::string GetComponentVersion(const std::string& name);

  // Get component metadata given component name.
  bool GetComponentMetadata(const std::string& name,
                            std::map<std::string, std::string>* out_metadata);

  // Load the specified component. This returns the mount point or an empty
  // string on failure.
  std::string LoadComponent(const std::string& name, HelperProcess* process);

  // Load the specified component at a set mount point.
  bool LoadComponent(const std::string& name, const std::string& mount_point,
                     HelperProcess* process);

  // Load the specified component from the given path.
  std::string LoadComponentAtPath(const std::string& name,
                                  const base::FilePath& absolute_path,
                                  HelperProcess* process);

  // The directory hierarchy for a component consists of the storage_root (i.e.
  // `/var/lib/imageloader`), the component_root
  // (`/var/lib/imageloader/ComponentName`), and the version folder (i.e.
  // `/var/lib/imageloader/ComponentName/23.0.0.205`). That is:
  // [storage_root]/
  // [storage_root]/[component_root]
  // [storage_root]/[component_root]/[version]
  //
  // Inside the `component_root` there is a current version hint file:
  // [storage_root]/[component_root]/latest-version

  // Return the path to latest-version file for |component_name|.
  base::FilePath GetLatestVersionFilePath(const std::string& component_name);

  // Return the path to the [component_root] folder for |component_name|.
  base::FilePath GetComponentRoot(const std::string& component_name);

  // Return the path to a given version of |component_name|.
  base::FilePath GetVersionPath(const std::string& component_name,
                                const std::string& version);

  // Return the path to the current version of |component_name|.
  bool GetPathToCurrentComponentVersion(const std::string& component_name,
                                        base::FilePath* result);

 private:
  FRIEND_TEST_ALL_PREFIXES(ImageLoaderTest, RemoveImageAtPathRemovable);
  FRIEND_TEST_ALL_PREFIXES(ImageLoaderTest, RemoveImageAtPathNotRemovable);

  // The configuration traits.
  ImageLoaderConfig config_;

  // Remove component if removable.
  bool RemoveComponentAtPath(const std::string& name,
                             const base::FilePath& component_root,
                             const base::FilePath& component_path);

  DISALLOW_COPY_AND_ASSIGN(ImageLoaderImpl);
};

}  // namespace imageloader

#endif  // IMAGELOADER_IMAGELOADER_IMPL_H_
