// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/libpreservation/filesystem_manager.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <init/libpreservation/ext2fs.h>

#include "base/functional/callback_helpers.h"

namespace libpreservation {
namespace {

bool ValidatePath(const base::FilePath& path) {
  if (!base::IsStringUTF8(path.value())) {
    return false;
  }

  auto path_components = path.GetComponents();

  if (path_components.empty()) {
    LOG(ERROR) << "Invalid path";
    return false;
  }

  // Validate that the path components don't contain special paths.
  for (auto& component : path_components) {
    if (component == base::FilePath::kCurrentDirectory ||
        component == base::FilePath::kParentDirectory) {
      LOG(ERROR) << "Invalid path component " << component;
      return false;
    }
  }

  return true;
}

}  // namespace

FilesystemManager::FilesystemManager(std::unique_ptr<Ext2fs> fs)
    : fs_(std::move(fs)) {}

bool FilesystemManager::CreateDirectory(const base::FilePath& path) {
  if (!ValidatePath(path)) {
    return false;
  }

  ext2_ino_t parent_inode = EXT2_ROOT_INO;

  base::FilePath parent_dir = path.DirName();
  // Check if parent directory exists.
  if (!parent_dir.value().empty()) {
    if (!fs_->LookupInode(parent_dir, &parent_inode)) {
      LOG(ERROR) << "Failed to look up parent directory: " << parent_dir;
      return false;
    }
  }

  if (!fs_->Mkdir(parent_inode, path.BaseName().value())) {
    LOG(ERROR) << "Failed to create directory: " << path;
    return false;
  }

  return true;
}

bool FilesystemManager::CreateFileAndFixedGoalFallocate(
    const base::FilePath& path, uint64_t size, const ExtentArray& extents) {
  if (!ValidatePath(path)) {
    return false;
  }

  ext2_ino_t parent_inode = EXT2_ROOT_INO;
  base::FilePath parent_dir = path.DirName();

  if (!fs_->LookupInode(parent_dir, &parent_inode)) {
    LOG(ERROR) << "Failed to look up parent directory: " << parent_dir;
    return false;
  }

  // Create new inode and link to the last component.
  ext2_ino_t new_inode;
  if (!fs_->NewInode(parent_inode, &new_inode)) {
    LOG(ERROR) << "Failed to create new inode for " << path;
    return false;
  }

  if (!fs_->LinkFile(parent_inode, path.BaseName().value(), new_inode)) {
    LOG(ERROR) << "Failed to link file: " << path;
    return false;
  }

  // Cleanup the file if we fail any operations from here on.
  base::ScopedClosureRunner cleanup(base::BindOnce(
      base::IgnoreResult(&Ext2fs::Unlink), base::Unretained(fs_.get()),
      parent_inode, path.BaseName().value()));

  // Set up inode attributes.
  struct ext2_inode inode;
  memset(&inode, 0, sizeof(struct ext2_inode));
  inode.i_mode = LINUX_S_IFREG | (0600 & ~fs_->GetUmask());
  inode.i_size = size & 0xFFFFFFFF;
  inode.i_size_high = size >> 32;
  inode.i_atime = inode.i_ctime = inode.i_mtime = time(0);
  inode.i_links_count = 1;

  // Set up extent header for inode.
  inode.i_flags &= ~EXT4_EXTENTS_FL;
  if (!fs_->InitInodeExtentHeader(new_inode, &inode)) {
    LOG(ERROR) << "Failed to setup extent header for " << path;
    return false;
  }

  // Write inode to disk.
  if (!fs_->PersistInode(new_inode, inode)) {
    LOG(ERROR) << "Failed to write inode for " << path;
    return false;
  }

  fs_->MarkInodeInUseAsFile(new_inode);

  // Prepare extents to fallocate() in order of physical goal to ensure that
  // the block allocator can always allocate the extent on an empty filesystem.
  std::map<blk64_t, Extent> extents_by_goal;

  for (auto& extent : extents.extent()) {
    extents_by_goal.insert({extent.goal(), extent});
  }

  // For each extent, fallocate() with a fixed goal.
  for (auto& [_, extent] : extents_by_goal) {
    if (!fs_->FixedGoalFallocate(new_inode, extent.goal(), extent.start(),
                                 extent.length())) {
      LOG(ERROR) << "Extent (start, length, goal): " << extent.start() << " "
                 << extent.length() << " " << extent.goal();
      LOG(ERROR) << "Failed to allocate extent for file: " << path;
      return false;
    }
  }

  cleanup.ReplaceClosure(base::DoNothing());

  return true;
}

bool FilesystemManager::UnlinkFile(const base::FilePath& path) {
  if (!ValidatePath(path)) {
    return false;
  }

  ext2_ino_t parent_inode = EXT2_ROOT_INO;

  base::FilePath parent_dir = path.DirName();

  if (!parent_dir.value().empty()) {
    if (!fs_->LookupInode(parent_dir, &parent_inode)) {
      LOG(ERROR) << "Failed to look up path: " << parent_dir;
      return false;
    }
  }

  if (!fs_->Unlink(parent_inode, path.BaseName().value())) {
    LOG(ERROR) << "Failed to unlink file: " << path;
    return false;
  }

  return true;
}

bool FilesystemManager::FileExists(const base::FilePath& path) {
  if (!ValidatePath(path)) {
    return false;
  }

  ext2_ino_t inode = EXT2_ROOT_INO;

  if (!fs_->LookupInode(path, &inode)) {
    return false;
  }

  return true;
}

}  // namespace libpreservation
