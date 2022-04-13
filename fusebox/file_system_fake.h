// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_FILE_SYSTEM_FAKE_H_
#define FUSEBOX_FILE_SYSTEM_FAKE_H_

#include <map>
#include <memory>

#include "fusebox/file_system.h"

// FileSystem fake implementation to process Kernel FUSE requests.

namespace fusebox {

class FakeFileEntry;  // FileSystemFake entry type.

class FileSystemFake : public FileSystem {
 public:
  FileSystemFake();
  FileSystemFake(const FileSystemFake&) = delete;
  FileSystemFake& operator=(const FileSystemFake&) = delete;
  virtual ~FileSystemFake();

  // FUSE lowlevel API: see <fuse_lowlevel.h> for API details.

  void Init(void* userdata, struct fuse_conn_info* conn) override;

  void Lookup(std::unique_ptr<EntryRequest> request,
              ino_t parent,
              const char* name) override;

  void GetAttr(std::unique_ptr<AttrRequest> request, ino_t ino) override;

  void SetAttr(std::unique_ptr<AttrRequest> request,
               ino_t ino,
               struct stat* attr,
               int to_set) override;

  void MkDir(std::unique_ptr<EntryRequest> request,
             ino_t parent,
             const char* name,
             mode_t mode) override;

  void Unlink(std::unique_ptr<OkRequest> request,
              ino_t parent,
              const char* name) override;

  void RmDir(std::unique_ptr<OkRequest> request,
             ino_t parent,
             const char* name) override;

  void Rename(std::unique_ptr<OkRequest> request,
              ino_t parent,
              const char* name,
              ino_t new_parent,
              const char* new_name) override;

  void OpenDir(std::unique_ptr<OpenRequest> request, ino_t ino) override;

  void ReadDir(std::unique_ptr<DirEntryRequest> request,
               ino_t ino,
               off_t off) override;

  void ReleaseDir(std::unique_ptr<OkRequest> request, ino_t ino) override;

  void Open(std::unique_ptr<OpenRequest> request, ino_t ino) override;

  void Read(std::unique_ptr<BufferRequest> request,
            ino_t ino,
            size_t size,
            off_t off) override;

  void Write(std::unique_ptr<WriteRequest> request,
             ino_t ino,
             const char* buf,
             size_t size,
             off_t off) override;

  void Release(std::unique_ptr<OkRequest> request, ino_t ino) override;

  void Create(std::unique_ptr<CreateRequest> request,
              ino_t parent,
              const char* name,
              mode_t mode) override;

 private:
  // True if the file system is read_only.
  bool read_only_ = false;

  // Map ino to file system fake file entry.
  std::map<ino_t, FakeFileEntry> files_;

  // Active readdir requests.
  std::map<uint64_t, std::unique_ptr<DirEntryResponse>> readdir_;
};

}  // namespace fusebox

#endif  // FUSEBOX_FILE_SYSTEM_FAKE_H_
