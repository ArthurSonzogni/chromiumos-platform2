// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <init/libpreservation/ext2fs.h>
#include <init/libpreservation/fake_ext2fs.h>

namespace libpreservation {

FakeExt2fs::FakeExt2fs() {
  inodes_.resize(EXT2_GOOD_OLD_FIRST_INO);
  inodes_[EXT2_ROOT_INO].in_use = true;
  path_to_inode_[base::FilePath("/")] = EXT2_ROOT_INO;
}

bool FakeExt2fs::LookupInode(const base::FilePath& path, ext2_ino_t* inode) {
  auto it = path_to_inode_.find(path);
  if (it == path_to_inode_.end()) {
    return false;
  }
  *inode = it->second;
  return true;
}

bool FakeExt2fs::Mkdir(ext2_ino_t parent, const std::string& name) {
  ext2_ino_t new_inode;
  if (!NewInode(parent, &new_inode)) {
    LOG(ERROR) << "Failed to create new inode";
    return false;
  }
  if (!LinkFile(parent, name, new_inode)) {
    LOG(ERROR) << "Failed to link file";
    return false;
  }

  inodes_[new_inode].is_dir = true;
  inodes_[new_inode].name = name;
  return true;
}

bool FakeExt2fs::NewInode(ext2_ino_t parent, ext2_ino_t* inode) {
  for (size_t i = EXT2_GOOD_OLD_FIRST_INO; i < inodes_.size(); ++i) {
    if (!inodes_[i].in_use) {
      inodes_[i].in_use = true;
      inodes_[i].parent = parent;
      *inode = i;
      return true;
    }
  }

  inodes_.push_back({.parent = parent, .in_use = true});
  *inode = inodes_.size() - 1;

  return true;
}

bool FakeExt2fs::LinkFile(ext2_ino_t parent,
                          const std::string& name,
                          ext2_ino_t inode) {
  base::FilePath path(name);
  while (parent != EXT2_ROOT_INO) {
    path = base::FilePath(inodes_[parent].name).Append(path);
    parent = inodes_[parent].parent;
  }

  path = base::FilePath("/").Append(path);
  path_to_inode_[path] = inode;
  return true;
}

bool FakeExt2fs::InitInodeExtentHeader(ext2_ino_t inode,
                                       ext2_inode* inode_struct) {
  inodes_[inode].extent_header_initialized = true;
  return true;
}

void FakeExt2fs::MarkInodeInUseAsFile(ext2_ino_t inode) {
  inodes_[inode].in_use = true;
}

bool FakeExt2fs::PersistInode(ext2_ino_t inode, ext2_inode inode_struct) {
  inodes_[inode].written_to_disk = true;
  return true;
}

bool FakeExt2fs::FixedGoalFallocate(ext2_ino_t inode,
                                    blk64_t goal,
                                    blk64_t start,
                                    blk64_t length) {
  // Check for overlap within file.
  for (const auto& extent : inodes_[inode].extents) {
    if (!(start + length < extent.start() ||
          start > extent.start() + extent.length())) {
      LOG(ERROR) << "Overlapping extent for inode: " << inode;
      return false;
    }
  }

  // Check for overlap in all allocated extents.
  for (const auto& [alloc_goal, alloc_len] : allocated_extents_) {
    if (!(goal + length < alloc_goal || goal > alloc_goal + alloc_len)) {
      LOG(ERROR) << "Overlapping extent for inode: " << inode;
      return false;
    }
  }

  Extent e;
  e.set_start(start);
  e.set_goal(goal);
  e.set_length(length);
  inodes_[inode].extents.push_back(e);
  allocated_extents_.push_back({goal, length});

  return true;
}

bool FakeExt2fs::Unlink(ext2_ino_t parent, const std::string& name) {
  base::FilePath path(name);
  while (parent != EXT2_ROOT_INO) {
    path = base::FilePath(inodes_[parent].name).Append(path);
    parent = inodes_[parent].parent;
  }

  path = base::FilePath("/").Append(path);

  // Mark inode as not in use
  if (path_to_inode_.find(path) == path_to_inode_.end()) {
    LOG(ERROR) << "File doesn't exist " << path;
    return false;
  }
  inodes_[path_to_inode_[path]].in_use = false;
  for (auto it = allocated_extents_.begin(); it != allocated_extents_.end();) {
    bool found = false;
    for (const auto& extent : inodes_[path_to_inode_[path]].extents) {
      if (it->first == extent.start() && it->second == extent.length()) {
        found = true;
        break;
      }
    }
    if (found) {
      it = allocated_extents_.erase(it);
    } else {
      ++it;
    }
  }

  inodes_[path_to_inode_[path]].extents.clear();
  path_to_inode_.erase(path);

  return true;
}

std::unique_ptr<Ext2fs> FakeExt2fs::Create(const base::FilePath& device) {
  return std::make_unique<FakeExt2fs>();
}

}  // namespace libpreservation
