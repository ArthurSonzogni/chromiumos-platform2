// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_LIBPRESERVATION_FAKE_EXT2FS_H_
#define INIT_LIBPRESERVATION_FAKE_EXT2FS_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <ext2fs/ext2fs.h>
#include <init/libpreservation/ext2fs.h>
#include <init/libpreservation/preseeded_files.pb.h>

namespace libpreservation {

struct Inode {
  ext2_ino_t parent;
  std::string name;
  bool extent_header_initialized;
  bool in_use;
  bool is_dir;
  bool written_to_disk;
  std::vector<Extent> extents;
};

class FakeExt2fs : public Ext2fs {
 public:
  FakeExt2fs();
  ~FakeExt2fs() override = default;

  mode_t GetUmask() override { return 022; }
  bool LookupInode(const base::FilePath& path, ext2_ino_t* inode) override;
  bool Mkdir(ext2_ino_t parent, const std::string& name) override;
  bool NewInode(ext2_ino_t parent, ext2_ino_t* inode) override;
  bool LinkFile(ext2_ino_t parent,
                const std::string& name,
                ext2_ino_t inode) override;
  bool InitInodeExtentHeader(ext2_ino_t inode,
                             ext2_inode* inode_struct) override;
  void MarkInodeInUseAsFile(ext2_ino_t inode) override;
  bool PersistInode(ext2_ino_t inode, ext2_inode inode_struct) override;
  bool FixedGoalFallocate(ext2_ino_t inode,
                          blk64_t goal,
                          blk64_t start,
                          blk64_t length) override;
  bool Unlink(ext2_ino_t parent, const std::string& name) override;

  static std::unique_ptr<Ext2fs> Create(const base::FilePath& device);

 private:
  std::vector<Inode> inodes_;
  std::map<base::FilePath, ext2_ino_t> path_to_inode_;
  std::vector<std::pair<blk64_t, blk64_t>> allocated_extents_;
};

}  // namespace libpreservation

#endif  // INIT_LIBPRESERVATION_FAKE_EXT2FS_H_
