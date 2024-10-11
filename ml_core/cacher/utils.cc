// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_core/cacher/utils.h"

#include <string>

#include <base/files/dir_reader_posix.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/files/file_util.h>

#include "ml_core/cacher/constants.h"

namespace {
const char kPrebuiltOpenCLCacheDir[] = "cl_cache";
constexpr size_t kProcModulesMaxFileSize = 65536;
const base::FilePath kProcModulesPath("/proc/modules");
}  // namespace

namespace cros {

base::FilePath PrebuiltOpenCLCacheDir(const base::FilePath& dlc_root_path) {
  return dlc_root_path.Append(kPrebuiltOpenCLCacheDir);
}

bool DirIsEmpty(const base::FilePath& source_dir) {
  bool is_empty = true;

  base::DirReaderPosix reader(kOpenCLCachingDir);
  if (!reader.IsValid()) {
    LOG(ERROR) << "Error opening cache directory";
    return is_empty;
  }

  while (reader.Next()) {
    // Don't count ".", ".."
    if (reader.name() == std::string(base::FilePath::kCurrentDirectory) ||
        reader.name() == std::string(base::FilePath::kParentDirectory)) {
      continue;
    }
    is_empty = false;
    break;
  }

  return is_empty;
}

bool NPUIsReady() {
  std::string contents;
  // Cache the value.
  static bool npu_is_ready =
      base::ReadFileToStringWithMaxSize(kProcModulesPath, &contents,
                                        kProcModulesMaxFileSize) &&
      contents.find("intel_vpu") != std::string::npos;
  return npu_is_ready;
}

// Deletes all the files in the cache in |target_dir|.
void ClearCacheDirectory(const base::FilePath& target_dir) {
  base::DirReaderPosix reader(target_dir.value().c_str());
  if (!reader.IsValid()) {
    LOG(ERROR) << "Error opening cache directory";
    return;
  }

  while (reader.Next()) {
    // Don't delete ".", ".."
    if (reader.name() == std::string(base::FilePath::kCurrentDirectory) ||
        reader.name() == std::string(base::FilePath::kParentDirectory)) {
      continue;
    }

    auto to_delete = target_dir.Append(reader.name());
    if (!brillo::DeleteFile(to_delete)) {
      LOG(ERROR) << "Error deleting " << to_delete;
    }
  }
}

// Will copy cache files from the source_dir into |target_dir|.
// It will overwrite any existing files of the same name.
void CopyCacheFiles(const base::FilePath& source_dir,
                    const base::FilePath& target_dir) {
  base::DirReaderPosix reader(source_dir.value().c_str());
  if (!reader.IsValid()) {
    LOG(ERROR) << "Error opening source directory";
    return;
  }

  while (reader.Next()) {
    auto source = source_dir.Append(reader.name());

    // Skip ".", ".." and symlinks
    if (reader.name() == std::string(base::FilePath::kCurrentDirectory) ||
        reader.name() == std::string(base::FilePath::kParentDirectory) ||
        base::IsLink(source)) {
      continue;
    }

    auto target = target_dir.Append(reader.name());
    LOG(INFO) << "Copying " << source << " to " << target_dir;
    if (!base::CopyFile(source, target)) {
      LOG(ERROR) << "Error copying " << source << " to " << target;
    }
  }
}

}  // namespace cros
