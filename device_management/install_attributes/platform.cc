// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Platform

#include "device_management/install_attributes/platform.h"

#include <limits>
#include <string>

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>

using base::FilePath;
using base::StringPrintf;

namespace device_management {

Platform::Platform() {}
Platform::~Platform() {}

void DcheckIsNonemptyAbsolutePath(const base::FilePath& path) {
  DCHECK(!path.empty());
  DCHECK(path.IsAbsolute()) << "path=" << path;
}

bool Platform::ReadFile(const FilePath& path, brillo::Blob* blob) {
  DCHECK(path.IsAbsolute()) << "path=" << path;

  int64_t file_size;
  if (!base::PathExists(path)) {
    return false;
  }
  if (!base::GetFileSize(path, &file_size)) {
    LOG(ERROR) << "Could not get size of " << path.value();
    return false;
  }
  // Compare to the max of a signed integer.
  if (file_size > static_cast<int64_t>(std::numeric_limits<int>::max())) {
    LOG(ERROR) << "File " << path.value() << " is too large: " << file_size
               << " bytes.";
    return false;
  }
  blob->resize(file_size);
  int data_read =
      base::ReadFile(path, reinterpret_cast<char*>(blob->data()), blob->size());
  // Cast is okay because of comparison to INT_MAX above.
  if (data_read != static_cast<int>(file_size)) {
    LOG(ERROR) << "Only read " << data_read << " of " << file_size << " bytes"
               << " from " << path.value() << ".";
    return false;
  }
  return true;
}

bool Platform::FileExists(const FilePath& path) const {
  DCHECK(path.IsAbsolute()) << "path=" << path;

  return base::PathExists(path);
}

bool Platform::DeleteFile(const FilePath& path) {
  DCHECK(path.IsAbsolute()) << "path=" << path;

  return brillo::DeleteFile(path);
}

bool Platform::SyncFileOrDirectory(const FilePath& path,
                                   bool is_directory,
                                   bool data_sync) {
  DCHECK(path.IsAbsolute()) << "path=" << path;

  return brillo::SyncFileOrDirectory(path, is_directory, data_sync);
}

bool Platform::SyncDirectory(const FilePath& path) {
  DCHECK(path.IsAbsolute()) << "path=" << path;

  return SyncFileOrDirectory(path, true /* directory */, false /* data_sync */);
}

bool Platform::WriteFileAtomic(const FilePath& path,
                               const brillo::Blob& blob,
                               mode_t mode) {
  DCHECK(path.IsAbsolute()) << "path=" << path;

  return brillo::WriteBlobToFileAtomic<brillo::Blob>(path, blob, mode);
}

bool Platform::WriteFileAtomicDurable(const FilePath& path,
                                      const brillo::Blob& blob,
                                      mode_t mode) {
  DCHECK(path.IsAbsolute()) << "path=" << path;

  const std::string data(reinterpret_cast<const char*>(blob.data()),
                         blob.size());
  if (!brillo::WriteToFileAtomic(path, data.data(), data.size(), mode))
    return false;
  return SyncDirectory(FilePath(path).DirName());
}
}  // namespace device_management
