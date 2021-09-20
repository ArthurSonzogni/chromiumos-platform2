// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_FILE_SYSTEM_H_
#define FUSEBOX_FILE_SYSTEM_H_

#include <memory>

#include "fusebox/fuse_request.h"

// FileSystem interface to process Kernel FUSE requests.

namespace fusebox {

class FileSystem {
 public:
  FileSystem();
  FileSystem(const FileSystem&) = delete;
  FileSystem& operator=(const FileSystem&) = delete;
  virtual ~FileSystem();

  // FUSE lowlevel API: see <fuse_lowlevel.h> for API details.

  virtual void Init(void* userdata, struct fuse_conn_info* conn);

  virtual void Destroy(void* userdata);

  virtual void Lookup(std::unique_ptr<EntryRequest> request,
                      fuse_ino_t parent,
                      const char* name);

  virtual void Forget(std::unique_ptr<NoneRequest> request,
                      fuse_ino_t ino,
                      uint64_t nlookup);

  virtual void GetAttr(std::unique_ptr<AttrRequest> request,
                       fuse_ino_t ino,
                       struct fuse_file_info* fi);

  virtual void SetAttr(std::unique_ptr<AttrRequest> request,
                       fuse_ino_t ino,
                       struct stat* attr,
                       int to_set,
                       struct fuse_file_info* fi);

  virtual void MkDir(std::unique_ptr<EntryRequest> request,
                     fuse_ino_t parent,
                     const char* name,
                     mode_t mode);

  virtual void Unlink(std::unique_ptr<OkRequest> request,
                      fuse_ino_t parent,
                      const char* name);

  virtual void RmDir(std::unique_ptr<OkRequest> request,
                     fuse_ino_t parent,
                     const char* name);

  virtual void Rename(std::unique_ptr<OkRequest> request,
                      fuse_ino_t parent,
                      const char* name,
                      fuse_ino_t new_parent,
                      const char* new_name);

  virtual void Open(std::unique_ptr<OpenRequest> request,
                    fuse_ino_t ino,
                    struct fuse_file_info* fi);

  virtual void Read(std::unique_ptr<BufferRequest> request,
                    fuse_ino_t ino,
                    size_t size,
                    off_t off,
                    struct fuse_file_info* fi);

  virtual void Write(std::unique_ptr<WriteRequest> request,
                     fuse_ino_t ino,
                     const char* buf,
                     size_t size,
                     off_t off,
                     struct fuse_file_info* fi);

  virtual void Release(std::unique_ptr<OkRequest> request,
                       fuse_ino_t ino,
                       struct fuse_file_info* fi);

  virtual void OpenDir(std::unique_ptr<OpenRequest> request,
                       fuse_ino_t ino,
                       struct fuse_file_info* fi);

  virtual void ReadDir(std::unique_ptr<DirEntryRequest> request,
                       fuse_ino_t ino,
                       off_t off,
                       struct fuse_file_info* fi);

  virtual void ReleaseDir(std::unique_ptr<OkRequest> request,
                          fuse_ino_t ino,
                          struct fuse_file_info* fi);

  virtual void Create(std::unique_ptr<CreateRequest> request,
                      fuse_ino_t parent,
                      const char* name,
                      mode_t mode,
                      struct fuse_file_info* fi);
};

}  // namespace fusebox

#endif  // FUSEBOX_FILE_SYSTEM_H_
