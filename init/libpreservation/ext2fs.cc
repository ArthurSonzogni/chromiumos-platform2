// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <init/libpreservation/ext2fs.h>

namespace libpreservation {

Ext2fsImpl::Ext2fsImpl(ext2_filsys fs) : fs_(fs) {}

Ext2fsImpl::~Ext2fsImpl() {
  if (fs_) {
    ext2fs_close(fs_);
  }
}

bool Ext2fsImpl::LookupInode(const base::FilePath& path, ext2_ino_t* inode) {
  errcode_t err;
  ext2_ino_t cwd = EXT2_ROOT_INO;
  err = ext2fs_namei(fs_, EXT2_ROOT_INO, cwd, path.value().c_str(), inode);
  if (err) {
    return false;
  }
  return true;
}

bool Ext2fsImpl::Mkdir(ext2_ino_t parent, const std::string& name) {
  errcode_t err;
  err = ext2fs_mkdir(fs_, parent, 0, name.c_str());
  if (err) {
    LOG(ERROR) << "Failed to create directory: " << name << err;
    return false;
  }
  return true;
}

bool Ext2fsImpl::NewInode(ext2_ino_t parent, ext2_ino_t* inode) {
  errcode_t err;
  err = ext2fs_new_inode(fs_, parent, LINUX_S_IFREG, nullptr, inode);
  if (err) {
    LOG(ERROR) << "Failed to create new inode; Error: " << err;
    return false;
  }
  return true;
}

bool Ext2fsImpl::LinkFile(ext2_ino_t parent,
                          const std::string& name,
                          ext2_ino_t inode) {
  errcode_t err;
  err = ext2fs_link(fs_, parent, name.c_str(), inode, EXT2_FT_REG_FILE);
  if (err) {
    LOG(ERROR) << "Failed to create link for file: " << name
               << "; Error: " << err;
    return false;
  }
  return true;
}

bool Ext2fsImpl::InitInodeExtentHeader(ext2_ino_t inode,
                                       ext2_inode* inode_struct) {
  errcode_t err;
  ext2_extent_handle_t handle;
  err = ext2fs_extent_open2(fs_, inode, inode_struct, &handle);
  if (err) {
    LOG(ERROR) << "Failed to setup extent header for inode: " << inode
               << "; Error: " << err;
    return false;
  }
  ext2fs_extent_free(handle);
  return true;
}

void Ext2fsImpl::MarkInodeInUseAsFile(ext2_ino_t inode) {
  ext2fs_inode_alloc_stats2(fs_, inode, /*inuse=*/1, /*isdir=*/0);
}

bool Ext2fsImpl::PersistInode(ext2_ino_t inode, ext2_inode inode_struct) {
  errcode_t err;
  err = ext2fs_write_new_inode(fs_, inode, &inode_struct);
  if (err) {
    LOG(ERROR) << "Failed to write inode: " << inode << "; Error: " << err;
    return err;
  }
  return true;
}

bool Ext2fsImpl::FixedGoalFallocate(ext2_ino_t inode,
                                    blk64_t goal,
                                    blk64_t start,
                                    blk64_t length) {
  errcode_t err;
  err = ext2fs_fallocate(fs_,
                         EXT2_FALLOCATE_INIT_BEYOND_EOF |
                             EXT2_FALLOCATE_FORCE_INIT |
                             EXT2_FALLOCATE_FIXED_GOAL,
                         inode, NULL, goal, start, length);
  if (err) {
    LOG(ERROR) << "Extent (start, length, goal): " << start << " " << length
               << " " << goal;
    LOG(ERROR) << "Failed to allocate extent for inode: " << inode
               << "; Error: " << err;
    return false;
  }
  return true;
}

bool Ext2fsImpl::Unlink(ext2_ino_t parent, const std::string& name) {
  errcode_t err;
  err = ext2fs_unlink(fs_, parent, name.c_str(), 0, 0);
  if (err) {
    LOG(ERROR) << "Failed to unlink file: " << name << "; Error: " << err;
    return false;
  }
  return true;
}

std::unique_ptr<Ext2fs> Ext2fsImpl::Generate(const base::FilePath& device) {
  ext2_filsys fs;

  if (ext2fs_open(device.value().c_str(), EXT2_FLAG_RW | EXT2_FLAG_DIRECT_IO, 0,
                  0, unix_io_manager, &fs)) {
    LOG(ERROR) << "Failed to open ext4 filesystem";
    return nullptr;
  }

  if (ext2fs_read_bitmaps(fs)) {
    LOG(ERROR) << "Failed to read bitmaps";
    return nullptr;
  }

  return std::make_unique<Ext2fsImpl>(fs);
}

}  // namespace libpreservation
