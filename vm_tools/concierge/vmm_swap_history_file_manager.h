// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_HISTORY_FILE_MANAGER_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_HISTORY_FILE_MANAGER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/files/file.h>

namespace vm_tools::concierge {

// Creates or deletes history file for vmm-swap policies.
//
// The file descriptor of returned `base::File` has `O_CLOEXEC`.
class VmmSwapHistoryFileManager final {
 public:
  explicit VmmSwapHistoryFileManager(base::FilePath path);
  VmmSwapHistoryFileManager(const VmmSwapHistoryFileManager&) = delete;
  VmmSwapHistoryFileManager& operator=(const VmmSwapHistoryFileManager&) =
      delete;
  ~VmmSwapHistoryFileManager() = default;

  // Creates the history file.
  //
  // If the file exists, this fails.
  base::File Create() const;

  // Creates a new history file for rotation.
  //
  // The rotation file is created in the directory of the history file and the
  // file name is ".tmp" suffixed.
  // If the file exists, this truncates the file and succeeds.
  base::File CreateRotationFile() const;

  // Open the history file.
  base::File Open() const;

  // Open a file in the directory of the history file.
  base::File OpenFileInDir(std::string file_name) const;

  // Atomically replace the history file with the rotation file.
  bool Rotate() const;

  // Deletes the history file.
  void Delete() const;

  // Deletes the rotation file.
  void DeleteRotationFile() const;

  // Deletes the file in the directory of the history file.
  void DeleteFileInDir(std::string file_name) const;

  // The path of the history file.
  const base::FilePath& path() const { return path_; }

 private:
  base::FilePath RotationFilePath() const;

  const base::FilePath path_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_HISTORY_FILE_MANAGER_H_
