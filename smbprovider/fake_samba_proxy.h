// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBPROVIDER_FAKE_SAMBA_PROXY_H_
#define SMBPROVIDER_FAKE_SAMBA_PROXY_H_

#include <string>
#include <vector>

#include "smbprovider/fake_samba_interface.h"
#include "smbprovider/samba_interface.h"

namespace smbprovider {

// Proxy for FakeSambaInterface. Allows us to take a unqiue_ptr to this class
// all backed by one FakeSambaInterface. All calls are direct pass throughs to
// FakeSambaInterface.
class FakeSambaProxy : public SambaInterface {
 public:
  explicit FakeSambaProxy(FakeSambaInterface* fake_samba_interface);
  ~FakeSambaProxy() override;

  // FakeSambaInterface overrides.
  int32_t OpenDirectory(const std::string& directory_path,
                        int32_t* dir_id) override;

  int32_t CloseDirectory(int32_t dir_id) override;

  int32_t GetDirectoryEntry(int32_t dir_id,
                            const struct smbc_dirent** dirent) override;

  int32_t GetDirectoryEntryWithMetadata(
      int32_t dir_id, const struct libsmb_file_info** file_info) override;

  int32_t GetEntryStatus(const std::string& entry_path,
                         struct stat* stat) override;

  int32_t OpenFile(const std::string& file_path,
                   int32_t flags,
                   int32_t* file_id) override;

  int32_t CloseFile(int32_t file_id) override;

  int32_t ReadFile(int32_t file_id,
                   uint8_t* buffer,
                   size_t buffer_size,
                   size_t* bytes_read) override;

  int32_t Seek(int32_t file_id, int64_t offset) override;

  int32_t Unlink(const std::string& file_path) override;

  int32_t RemoveDirectory(const std::string& dir_path) override;

  int32_t CreateFile(const std::string& file_path, int32_t* file_id) override;

  int32_t Truncate(int32_t file_id, size_t size) override;

  int32_t WriteFile(int32_t file_id,
                    const uint8_t* buffer,
                    size_t buffer_size) override;

  int32_t CreateDirectory(const std::string& directory_path) override;

  int32_t MoveEntry(const std::string& source_path,
                    const std::string& target_path) override;

  int32_t CopyFile(const std::string& source_path,
                   const std::string& target_path) override;

  int32_t SpliceFile(int32_t source_fd,
                     int32_t target_fd,
                     off_t length,
                     off_t* bytes_written) override;

  SambaInterface::SambaInterfaceId GetSambaInterfaceId() override;

 private:
  FakeSambaInterface* fake_samba_interface_;  // Not owned.
  SambaInterface::SambaInterfaceId samba_interface_id_;

  DISALLOW_COPY_AND_ASSIGN(FakeSambaProxy);
};

}  // namespace smbprovider

#endif  // SMBPROVIDER_FAKE_SAMBA_PROXY_H_
