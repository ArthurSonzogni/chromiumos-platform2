// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_LIBPRESERVATION_FILE_PRESEEDER_H_
#define INIT_LIBPRESERVATION_FILE_PRESEEDER_H_

#include <set>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <init/libpreservation/filesystem_manager.h>
#include <init/libpreservation/preseeded_files.pb.h>

namespace libpreservation {
// Generic class for file preseeding across a filesystem reset:
// this is used in two scenarios:
// 1) Powerwash: preserve files across a TPM reset.
// 2) Startup: preserve files across the establishment of a new
//    dm-default-key layer.
class BRILLO_EXPORT FilePreseeder {
 public:
  FilePreseeder(const std::set<base::FilePath>& directory_allowlist,
                const base::FilePath& fs_root,
                const base::FilePath& mount_root,
                const base::FilePath& metadata_path);
  virtual ~FilePreseeder();

  virtual bool SaveFileState(const std::set<base::FilePath>& file_list);

  // Intended to be used for crash resilience.
  bool PersistMetadata();
  bool LoadMetadata();

  bool CheckAllowlist(const base::FilePath& path);
  bool CreateDirectoryRecursively(FilesystemManager* fs_manager,
                                  const base::FilePath& path);
  bool RestoreExtentFiles(FilesystemManager* fs_manager);
  bool RestoreInlineFiles();

 protected:
  virtual bool GetFileExtents(const base::FilePath& file, ExtentArray* extents);

 private:
  // Both allowlists are relative to the root of the filesystem.
  const std::set<base::FilePath> directory_allowlist_;
  const base::FilePath fs_root_;
  const base::FilePath mount_root_;
  const base::FilePath metadata_path_;
  PreseededFileArray preseeded_files_;
};

}  // namespace libpreservation

#endif  // INIT_LIBPRESERVATION_FILE_PRESEEDER_H_
