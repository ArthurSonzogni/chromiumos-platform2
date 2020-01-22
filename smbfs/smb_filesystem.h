// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_SMB_FILESYSTEM_H_
#define SMBFS_SMB_FILESYSTEM_H_

#include <libsmbclient.h>
#include <sys/types.h>

#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/macros.h>
#include <base/threading/thread.h>

#include "smbfs/filesystem.h"
#include "smbfs/inode_map.h"
#include "smbfs/smb_credential.h"

namespace smbfs {

class SmbFilesystem : public Filesystem {
 public:
  enum class ConnectError {
    kOk = 0,
    kNotFound,
    kAccessDenied,
    kSmb1Unsupported,
    kUnknownError,
  };

  SmbFilesystem(const std::string& share_path,
                uid_t uid,
                gid_t gid,
                std::unique_ptr<SmbCredential> credentials);
  ~SmbFilesystem() override;

  // Ensures that the SMB share can be connected to. Must NOT be called after
  // the filesystem is attached to a FUSE session.
  ConnectError EnsureConnected();

  // Filesystem overrides.
  void Lookup(std::unique_ptr<EntryRequest> request,
              fuse_ino_t parent_inode,
              const std::string& name) override;
  void Forget(fuse_ino_t inode, uint64_t count) override;
  void GetAttr(std::unique_ptr<AttrRequest> request, fuse_ino_t inode) override;
  void SetAttr(std::unique_ptr<AttrRequest> request,
               fuse_ino_t inode,
               base::Optional<uint64_t> file_handle,
               const struct stat& attr,
               int to_set) override;
  void Open(std::unique_ptr<OpenRequest> request,
            fuse_ino_t inode,
            int flags) override;
  void Create(std::unique_ptr<CreateRequest> request,
              fuse_ino_t parent_inode,
              const std::string& name,
              mode_t mode,
              int flags) override;
  void Read(std::unique_ptr<BufRequest> request,
            fuse_ino_t inode,
            uint64_t file_handle,
            size_t size,
            off_t offset) override;
  void Write(std::unique_ptr<WriteRequest> request,
             fuse_ino_t inode,
             uint64_t file_handle,
             const char* buf,
             size_t size,
             off_t offset) override;
  void Release(std::unique_ptr<SimpleRequest> request,
               fuse_ino_t inode,
               uint64_t file_handle) override;
  void OpenDir(std::unique_ptr<OpenRequest> request,
               fuse_ino_t inode,
               int flags) override;
  void ReadDir(std::unique_ptr<DirentryRequest> request,
               fuse_ino_t inode,
               uint64_t file_handle,
               off_t offset) override;
  void ReleaseDir(std::unique_ptr<SimpleRequest> request,
                  fuse_ino_t inode,
                  uint64_t file_handle) override;

 private:
  // Filesystem implementations that execute on |samba_thread_|.
  void LookupInternal(std::unique_ptr<EntryRequest> request,
                      fuse_ino_t parent_inode,
                      const std::string& name);
  void ForgetInternal(fuse_ino_t inode, uint64_t count);
  void GetAttrInternal(std::unique_ptr<AttrRequest> request, fuse_ino_t inode);
  void SetAttrInternal(std::unique_ptr<AttrRequest> request,
                       fuse_ino_t inode,
                       base::Optional<uint64_t> file_handle,
                       const struct stat& attr,
                       int to_set);
  void OpenInternal(std::unique_ptr<OpenRequest> request,
                    fuse_ino_t inode,
                    int flags);
  void CreateInternal(std::unique_ptr<CreateRequest> request,
                      fuse_ino_t parent_inode,
                      const std::string& name,
                      mode_t mode,
                      int flags);
  void ReadInternal(std::unique_ptr<BufRequest> request,
                    fuse_ino_t inode,
                    uint64_t file_handle,
                    size_t size,
                    off_t offset);
  void WriteInternal(std::unique_ptr<WriteRequest> request,
                     fuse_ino_t inode,
                     uint64_t file_handle,
                     const std::vector<char>& buf,
                     off_t offset);
  void ReleaseInternal(std::unique_ptr<SimpleRequest> request,
                       fuse_ino_t inode,
                       uint64_t file_handle);
  void OpenDirInternal(std::unique_ptr<OpenRequest> request,
                       fuse_ino_t inode,
                       int flags);
  void ReadDirInternal(std::unique_ptr<DirentryRequest> request,
                       fuse_ino_t inode,
                       uint64_t file_handle,
                       off_t offset);
  void ReleaseDirInternal(std::unique_ptr<SimpleRequest> request,
                          fuse_ino_t inode,
                          uint64_t file_handle);

  // Constructs a sanitised stat struct for sending as a response.
  struct stat MakeStat(ino_t inode, const struct stat& in_stat) const;

  // Constructs a share file path suitable for passing to libsmbclient from the
  // given absolute file path.
  std::string MakeShareFilePath(const base::FilePath& path) const;

  // Construct a share file path from the |inode|. |inode| must be a valid inode
  // number.
  std::string ShareFilePathFromInode(ino_t inode) const;

  // Registers an open file and returns a handle to that file. Always returns a
  // non-zero handle.
  uint64_t AddOpenFile(SMBCFILE* file);

  // Removes |handle| from the open file table.
  void RemoveOpenFile(uint64_t handle);

  // Returns the open file referred to by |handle|. Returns nullptr if |handle|
  // does not exist.
  SMBCFILE* LookupOpenFile(uint64_t handle) const;

  // Callback function for obtaining authentication credentials. Set by calling
  // smbc_setFunctionAuthDataWithContext() and called from libsmbclient.
  static void GetUserAuth(SMBCCTX* context,
                          const char* server,
                          const char* share,
                          char* workgroup,
                          int workgroup_len,
                          char* username,
                          int username_len,
                          char* password,
                          int password_len);

  const std::string share_path_;
  const uid_t uid_;
  const gid_t gid_;
  const std::unique_ptr<SmbCredential> credentials_;
  base::Thread samba_thread_;
  InodeMap inode_map_{FUSE_ROOT_ID};

  std::unordered_map<uint64_t, SMBCFILE*> open_files_;
  uint64_t open_files_seq_ = 1;

  SMBCCTX* context_ = nullptr;
  smbc_close_fn smbc_close_ctx_ = nullptr;
  smbc_closedir_fn smbc_closedir_ctx_ = nullptr;
  smbc_ftruncate_fn smbc_ftruncate_ctx_ = nullptr;
  smbc_lseek_fn smbc_lseek_ctx_ = nullptr;
  smbc_lseekdir_fn smbc_lseekdir_ctx_ = nullptr;
  smbc_open_fn smbc_open_ctx_ = nullptr;
  smbc_opendir_fn smbc_opendir_ctx_ = nullptr;
  smbc_read_fn smbc_read_ctx_ = nullptr;
  smbc_readdir_fn smbc_readdir_ctx_ = nullptr;
  smbc_stat_fn smbc_stat_ctx_ = nullptr;
  smbc_telldir_fn smbc_telldir_ctx_ = nullptr;
  smbc_write_fn smbc_write_ctx_ = nullptr;

  DISALLOW_IMPLICIT_CONSTRUCTORS(SmbFilesystem);
};

std::ostream& operator<<(std::ostream& out, SmbFilesystem::ConnectError error);

}  // namespace smbfs

#endif  // SMBFS_SMB_FILESYSTEM_H_
