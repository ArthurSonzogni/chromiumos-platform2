// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_LIBPRESERVATION_EXT2FS_H_
#define INIT_LIBPRESERVATION_EXT2FS_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <ext2fs/ext2fs.h>

namespace libpreservation {

// Abstract ext2/3/4 filesystem interface for an open filesystem.
class Ext2fs {
 public:
  virtual ~Ext2fs() = default;
  // Get umask for filesystem.
  virtual mode_t GetUmask() = 0;
  // Look up inode number for a given path, return false on failure.
  virtual bool LookupInode(const base::FilePath& path, ext2_ino_t* inode) = 0;
  // Create a new directory.
  virtual bool Mkdir(ext2_ino_t parent, const std::string& name) = 0;
  // Create a new inode.
  virtual bool NewInode(ext2_ino_t parent, ext2_ino_t* inode) = 0;
  // Link inode to an entry under |parent| at |name|.
  virtual bool LinkFile(ext2_ino_t parent,
                        const std::string& name,
                        ext2_ino_t inode) = 0;
  // Initialize the inode extent header with the |inode_struct|.
  virtual bool InitInodeExtentHeader(ext2_ino_t inode,
                                     ext2_inode* inode_struct) = 0;
  // Mark inode in use as a file.
  virtual void MarkInodeInUseAsFile(ext2_ino_t inode) = 0;
  // Persists inode to disk.
  virtual bool PersistInode(ext2_ino_t inode, ext2_inode inode_struct) = 0;
  // Fallocate an extent at a fixed |goal| on disk.
  virtual bool FixedGoalFallocate(ext2_ino_t inode,
                                  blk64_t goal,
                                  blk64_t start,
                                  blk64_t length) = 0;
  // Unlink name from |parent|.
  virtual bool Unlink(ext2_ino_t parent, const std::string& name) = 0;
};

// Implementation of Ext2fs that calls into libe2fsprogs.
class Ext2fsImpl : public Ext2fs {
 public:
  explicit Ext2fsImpl(ext2_filsys fs);
  ~Ext2fsImpl() override;

  mode_t GetUmask() override { return fs_->umask; }
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

  static std::unique_ptr<Ext2fs> Generate(const base::FilePath& device);

 private:
  ext2_filsys fs_;
};

}  // namespace libpreservation

#endif  // INIT_LIBPRESERVATION_EXT2FS_H_
