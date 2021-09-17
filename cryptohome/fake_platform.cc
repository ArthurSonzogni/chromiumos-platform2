// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Platform

#include "cryptohome/fake_platform.h"

#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

namespace {

class ProxyFileEnumerator : public FileEnumerator {
 public:
  ProxyFileEnumerator(const base::FilePath& tmpfs_rootfs,
                      FakePlatform* fake_platform,
                      FileEnumerator* real_enumerator)
      : tmpfs_rootfs_(tmpfs_rootfs),
        fake_platform_(fake_platform),
        real_enumerator_(real_enumerator) {}

  // Removed tmpfs prefix from the returned path.
  base::FilePath Next() override {
    base::FilePath next = real_enumerator_->Next();
    if (!tmpfs_rootfs_.IsParent(next)) {
      return next;
    }
    base::FilePath assumed_path("/");
    CHECK(tmpfs_rootfs_.AppendRelativePath(next, &assumed_path));
    last_path_ = assumed_path;
    return assumed_path;
  }

  FileEnumerator::FileInfo GetInfo() override {
    FileEnumerator::FileInfo real_info = real_enumerator_->GetInfo();
    base::stat_wrapper_t stat;
    DCHECK(fake_platform_->Stat(last_path_, &stat));
    return FileEnumerator::FileInfo(real_info.GetName(), stat);
  }

 private:
  base::FilePath tmpfs_rootfs_;
  base::FilePath last_path_;
  FakePlatform* fake_platform_;
  std::unique_ptr<FileEnumerator> real_enumerator_;
};

template <typename KeyType>
void RemoveFakeEntriesRecursiveImpl(
    const base::FilePath& path,
    std::unordered_map<base::FilePath, KeyType>* m) {
  for (auto it = m->begin(); it != m->end();) {
    auto tmp_it = it;
    ++it;
    if (tmp_it->first == path || path.IsParent(tmp_it->first)) {
      m->erase(tmp_it);
    }
  }
}

base::FilePath NormalizePath(const base::FilePath& path) {
  std::vector<std::string> components;
  std::vector<std::string> normalized_components;

  path.GetComponents(&components);
  for (const auto& component : components) {
    if (component == ".") {
      continue;
    }
    if (component == "..") {
      normalized_components.pop_back();
      continue;
    }
    normalized_components.push_back(component);
  }

  base::FilePath result;
  for (const auto& component : normalized_components) {
    if (result.empty()) {
      result = base::FilePath(component);
      continue;
    }
    result = result.Append(component);
  }
  return result;
}

}  // namespace

// FakeExtendedAttributes

bool FakePlatform::FakeExtendedAttributes::Exists(
    const std::string& name) const {
  return xattrs_.find(name) != xattrs_.end();
}

void FakePlatform::FakeExtendedAttributes::List(
    std::vector<std::string>* attr_list) const {
  DCHECK(attr_list);
  attr_list->clear();
  for (const auto& xattr : xattrs_) {
    attr_list->push_back(xattr.first);
  }
}

bool FakePlatform::FakeExtendedAttributes::GetAsString(
    const std::string& name, std::string* value) const {
  const auto it = xattrs_.find(name);
  if (it == xattrs_.end()) {
    return false;
  }

  value->assign(it->second.data(), it->second.size());
  return true;
}

bool FakePlatform::FakeExtendedAttributes::Get(const std::string& name,
                                               char* value,
                                               ssize_t size) const {
  const auto it = xattrs_.find(name);
  if (it == xattrs_.end()) {
    return false;
  }

  if (it->second.size() > size) {
    return false;
  }

  memcpy(value, it->second.data(), it->second.size());

  return true;
}

void FakePlatform::FakeExtendedAttributes::Set(const std::string& name,
                                               const char* value,
                                               ssize_t size) {
  xattrs_[name].assign(value, value + size);
}

void FakePlatform::FakeExtendedAttributes::Remove(const std::string& name) {
  xattrs_.erase(name);
}

// Constructor/destructor

FakePlatform::FakePlatform() : Platform() {
  base::GetTempDir(&tmpfs_rootfs_);
  tmpfs_rootfs_ = tmpfs_rootfs_.Append(real_platform_.GetRandomSuffix());
  if (!real_platform_.CreateDirectory(tmpfs_rootfs_)) {
    LOG(ERROR) << "Failed to create test dir: " << tmpfs_rootfs_;
  }
}

FakePlatform::~FakePlatform() {
  real_platform_.DeletePathRecursively(tmpfs_rootfs_);
}

// Helpers

base::FilePath FakePlatform::TestFilePath(const base::FilePath& path) const {
  std::string path_str = path.NormalizePathSeparators().value();
  // Make the path relative.
  CHECK(path.IsAbsolute());
  if (path_str.length() > 0 && path_str[0] == '/') {
    path_str = path_str.substr(1);
  }
  return tmpfs_rootfs_.Append(path_str);
}

base::FilePath FakePlatform::StripTestFilePath(
    const base::FilePath& path) const {
  base::FilePath result("/");
  if (!tmpfs_rootfs_.AppendRelativePath(path, &result)) {
    // Not under the test root, so return as is.
    return path;
  }
  return result;
}

bool FakePlatform::IsLink(const base::FilePath& path) const {
  return base::IsLink(TestFilePath(path));
}

void FakePlatform::RemoveFakeEntries(const base::FilePath& path) {
  xattrs_.erase(path);
  file_owners_.erase(path);
  file_mode_.erase(path);
}

void FakePlatform::RemoveFakeEntriesRecursive(const base::FilePath& path) {
  RemoveFakeEntriesRecursiveImpl(path, &xattrs_);
  RemoveFakeEntriesRecursiveImpl(path, &file_owners_);
  RemoveFakeEntriesRecursiveImpl(path, &file_mode_);
}

// Platform API

bool FakePlatform::Rename(const base::FilePath& from,
                          const base::FilePath& to) {
  return real_platform_.Rename(TestFilePath(from), TestFilePath(to));
}

bool FakePlatform::Move(const base::FilePath& from, const base::FilePath& to) {
  return real_platform_.Move(TestFilePath(from), TestFilePath(to));
}

bool FakePlatform::Copy(const base::FilePath& from, const base::FilePath& to) {
  return real_platform_.Copy(TestFilePath(from), TestFilePath(to));
}

bool FakePlatform::EnumerateDirectoryEntries(
    const base::FilePath& path,
    bool recursive,
    std::vector<base::FilePath>* ent_list) {
  return real_platform_.EnumerateDirectoryEntries(TestFilePath(path), recursive,
                                                  ent_list);
}

bool FakePlatform::IsDirectoryEmpty(const base::FilePath& path) {
  return real_platform_.IsDirectoryEmpty(TestFilePath(path));
}

bool FakePlatform::TouchFileDurable(const base::FilePath& path) {
  return real_platform_.TouchFileDurable(TestFilePath(path));
}

bool FakePlatform::DeleteFile(const base::FilePath& path) {
  RemoveFakeEntries(path);
  return real_platform_.DeleteFile(TestFilePath(path));
}

bool FakePlatform::DeletePathRecursively(const base::FilePath& path) {
  RemoveFakeEntriesRecursive(path);
  return real_platform_.DeletePathRecursively(TestFilePath(path));
}

bool FakePlatform::DeleteFileDurable(const base::FilePath& path) {
  RemoveFakeEntries(path);
  return real_platform_.DeleteFileDurable(TestFilePath(path));
}

bool FakePlatform::FileExists(const base::FilePath& path) {
  return real_platform_.FileExists(TestFilePath(path));
}

bool FakePlatform::DirectoryExists(const base::FilePath& path) {
  return real_platform_.DirectoryExists(TestFilePath(path));
}

bool FakePlatform::CreateDirectory(const base::FilePath& path) {
  return real_platform_.CreateDirectory(TestFilePath(path));
}

bool FakePlatform::CreateSparseFile(const base::FilePath& path, int64_t size) {
  return real_platform_.CreateSparseFile(TestFilePath(path), size);
}

bool FakePlatform::DataSyncFile(const base::FilePath& path) {
  return real_platform_.DataSyncFile(TestFilePath(path));
}

bool FakePlatform::SyncFile(const base::FilePath& path) {
  return real_platform_.SyncFile(TestFilePath(path));
}

bool FakePlatform::SyncDirectory(const base::FilePath& path) {
  return real_platform_.SyncDirectory(TestFilePath(path));
}

void FakePlatform::Sync() {
  real_platform_.Sync();
}

bool FakePlatform::CreateSymbolicLink(const base::FilePath& path,
                                      const base::FilePath& target) {
  if (target.IsAbsolute()) {
    return real_platform_.CreateSymbolicLink(TestFilePath(path),
                                             TestFilePath(target));
  } else {
    return real_platform_.CreateSymbolicLink(TestFilePath(path), target);
  }
}

bool FakePlatform::ReadLink(const base::FilePath& path,
                            base::FilePath* target) {
  base::FilePath tmp_path;
  if (!real_platform_.ReadLink(TestFilePath(path), &tmp_path)) {
    return false;
  }

  *target = StripTestFilePath(tmp_path);
  return true;
}

bool FakePlatform::SetFileTimes(const base::FilePath& path,
                                const struct timespec& atime,
                                const struct timespec& mtime,
                                bool follow_links) {
  return real_platform_.SetFileTimes(TestFilePath(path), atime, mtime,
                                     follow_links);
}

bool FakePlatform::SendFile(int fd_to,
                            int fd_from,
                            off_t offset,
                            size_t count) {
  return real_platform_.SendFile(fd_to, fd_from, offset, count);
}

void FakePlatform::InitializeFile(base::File* file,
                                  const base::FilePath& path,
                                  uint32_t flags) {
  // This part here is to make one of the access verification tests happy.
  // TODO(dlunev): generalize access control abiding fake permissions.
  mode_t mode;
  DCHECK(GetPermissions(path, &mode));
  bool init_for_read = flags & base::File::FLAG_READ;
  bool can_read = mode & S_IRUSR;
  if (init_for_read && !can_read) {
    return;
  }

  real_platform_.InitializeFile(file, TestFilePath(path), flags);
}

bool FakePlatform::LockFile(int fd) {
  return real_platform_.LockFile(fd);
}

bool FakePlatform::ReadFile(const base::FilePath& path, brillo::Blob* blob) {
  return real_platform_.ReadFile(TestFilePath(path), blob);
}

bool FakePlatform::ReadFileToString(const base::FilePath& path,
                                    std::string* str) {
  return real_platform_.ReadFileToString(TestFilePath(path), str);
}

bool FakePlatform::ReadFileToSecureBlob(const base::FilePath& path,
                                        brillo::SecureBlob* sblob) {
  return real_platform_.ReadFileToSecureBlob(TestFilePath(path), sblob);
}

bool FakePlatform::WriteFile(const base::FilePath& path,
                             const brillo::Blob& blob) {
  return real_platform_.WriteFile(TestFilePath(path), blob);
}

bool FakePlatform::WriteSecureBlobToFile(const base::FilePath& path,
                                         const brillo::SecureBlob& sblob) {
  return real_platform_.WriteSecureBlobToFile(TestFilePath(path), sblob);
}

bool FakePlatform::WriteFileAtomic(const base::FilePath& path,
                                   const brillo::Blob& blob,
                                   mode_t mode) {
  return real_platform_.WriteFileAtomic(TestFilePath(path), blob, mode);
}

bool FakePlatform::WriteSecureBlobToFileAtomic(const base::FilePath& path,
                                               const brillo::SecureBlob& sblob,
                                               mode_t mode) {
  return real_platform_.WriteSecureBlobToFileAtomic(TestFilePath(path), sblob,
                                                    mode);
}

bool FakePlatform::WriteFileAtomicDurable(const base::FilePath& path,
                                          const brillo::Blob& blob,
                                          mode_t mode) {
  return real_platform_.WriteFileAtomicDurable(TestFilePath(path), blob, mode);
}

bool FakePlatform::WriteSecureBlobToFileAtomicDurable(
    const base::FilePath& path, const brillo::SecureBlob& sblob, mode_t mode) {
  return real_platform_.WriteSecureBlobToFileAtomicDurable(TestFilePath(path),
                                                           sblob, mode);
}

bool FakePlatform::WriteStringToFile(const base::FilePath& path,
                                     const std::string& str) {
  return real_platform_.WriteStringToFile(TestFilePath(path), str);
}

bool FakePlatform::WriteStringToFileAtomicDurable(const base::FilePath& path,
                                                  const std::string& str,
                                                  mode_t mode) {
  return real_platform_.WriteStringToFileAtomicDurable(TestFilePath(path), str,
                                                       mode);
}

bool FakePlatform::WriteArrayToFile(const base::FilePath& path,
                                    const char* data,
                                    size_t size) {
  return real_platform_.WriteArrayToFile(TestFilePath(path), data, size);
}

FILE* FakePlatform::OpenFile(const base::FilePath& path, const char* mode) {
  return real_platform_.OpenFile(TestFilePath(path), mode);
}

bool FakePlatform::CloseFile(FILE* file) {
  return real_platform_.CloseFile(file);
}

FileEnumerator* FakePlatform::GetFileEnumerator(const base::FilePath& path,
                                                bool recursive,
                                                int file_type) {
  return new ProxyFileEnumerator(tmpfs_rootfs_, this,
                                 real_platform_.GetFileEnumerator(
                                     TestFilePath(path), recursive, file_type));
}

bool FakePlatform::GetFileSize(const base::FilePath& path, int64_t* size) {
  return real_platform_.GetFileSize(TestFilePath(path), size);
}

bool FakePlatform::Stat(const base::FilePath& path, base::stat_wrapper_t* buf) {
  if (!real_platform_.Stat(TestFilePath(path), buf)) {
    return false;
  }
  // Override mode and ownership from internal fake mappings.
  mode_t mode;
  if (!GetPermissions(path, &mode)) {
    return false;
  }
  buf->st_mode &= ~01777;
  buf->st_mode |= mode;
  if (!GetOwnership(path, &buf->st_uid, &buf->st_gid, false)) {
    return false;
  }
  return true;
}

bool FakePlatform::HasExtendedFileAttribute(const base::FilePath& path,
                                            const std::string& name) {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  if (!IsLink(npath) && !FileExists(npath)) {
    return false;
  }
  const auto it = xattrs_.find(npath);
  if (it == xattrs_.end() || !it->second.Exists(name)) {
    // Client code checks the error code, so set it.
    errno = ENODATA;
    return false;
  }

  return true;
}

bool FakePlatform::ListExtendedFileAttributes(
    const base::FilePath& path, std::vector<std::string>* attr_list) {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  if (!IsLink(npath) && !FileExists(npath)) {
    return false;
  }
  const auto it = xattrs_.find(npath);
  if (it == xattrs_.end()) {
    attr_list->clear();
    return true;
  }

  it->second.List(attr_list);
  return true;
}

bool FakePlatform::GetExtendedFileAttributeAsString(const base::FilePath& path,
                                                    const std::string& name,
                                                    std::string* value) {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  if (!IsLink(npath) && !FileExists(npath)) {
    return false;
  }
  const auto it = xattrs_.find(npath);
  if (it == xattrs_.end() || !it->second.Exists(name)) {
    // Client code checks the error code, so set it.
    errno = ENODATA;
    return false;
  }

  return it->second.GetAsString(name, value);
}

bool FakePlatform::GetExtendedFileAttribute(const base::FilePath& path,
                                            const std::string& name,
                                            char* value,
                                            ssize_t size) {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  if (!IsLink(npath) && !FileExists(npath)) {
    return false;
  }
  const auto it = xattrs_.find(npath);
  if (it == xattrs_.end() || !it->second.Exists(name)) {
    // Client code checks the error code, so set it.
    errno = ENODATA;
    return false;
  }

  return it->second.Get(name, value, size);
}

bool FakePlatform::SetExtendedFileAttribute(const base::FilePath& path,
                                            const std::string& name,
                                            const char* value,
                                            size_t size) {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  if (!IsLink(npath) && !FileExists(npath)) {
    return false;
  }

  auto [it, unused] =
      xattrs_.emplace(npath, FakePlatform::FakeExtendedAttributes());

  it->second.Set(name, value, size);
  return true;
}

bool FakePlatform::RemoveExtendedFileAttribute(const base::FilePath& path,
                                               const std::string& name) {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  if (!IsLink(npath) && !FileExists(npath)) {
    return false;
  }
  auto it = xattrs_.find(npath);
  if (it == xattrs_.end()) {
    return true;
  }

  it->second.Remove(name);
  return true;
}

bool FakePlatform::GetExtFileAttributes(const base::FilePath& path,
                                        int* flags) {
  return real_platform_.GetExtFileAttributes(TestFilePath(path), flags);
}

bool FakePlatform::SetExtFileAttributes(const base::FilePath& path, int flags) {
  return real_platform_.SetExtFileAttributes(TestFilePath(path), flags);
}

bool FakePlatform::HasNoDumpFileAttribute(const base::FilePath& path) {
  return real_platform_.HasNoDumpFileAttribute(TestFilePath(path));
}

bool FakePlatform::GetOwnership(const base::FilePath& path,
                                uid_t* user_id,
                                gid_t* group_id,
                                bool follow_links) const {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  // TODO(chromium:1141301, dlunev): here and further check for file existence.
  // Can not do it at present due to weird test dependencies.
  if (file_owners_.find(npath) == file_owners_.end()) {
    *user_id = fake_platform::kChronosUID;
    *group_id = fake_platform::kChronosGID;
    return true;
  }

  *user_id = file_owners_.at(npath).first;
  *group_id = file_owners_.at(npath).second;
  return true;
}

bool FakePlatform::SetOwnership(const base::FilePath& path,
                                uid_t user_id,
                                gid_t group_id,
                                bool follow_links) const {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  file_owners_[npath] = {user_id, group_id};
  return true;
}

bool FakePlatform::GetPermissions(const base::FilePath& path,
                                  mode_t* mode) const {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  if (file_mode_.find(npath) == file_mode_.end()) {
    (*mode) = S_IRWXU | S_IRGRP | S_IXGRP;
    return true;
  }
  (*mode) = (file_mode_.at(npath) & 01777);
  return true;
}

bool FakePlatform::SetPermissions(const base::FilePath& path,
                                  mode_t mode) const {
  base::AutoLock lock(mappings_lock_);
  base::FilePath npath = NormalizePath(path);
  file_mode_[npath] = mode & 01777;
  return true;
}

bool FakePlatform::GetUserId(const std::string& user,
                             uid_t* user_id,
                             gid_t* group_id) const {
  CHECK(user_id);
  CHECK(group_id);

  if (uids_.find(user) == uids_.end() || gids_.find(user) == gids_.end()) {
    LOG(ERROR) << "No user: " << user;
    return false;
  }

  *user_id = uids_.at(user);
  *group_id = gids_.at(user);
  return true;
}

bool FakePlatform::GetGroupId(const std::string& group, gid_t* group_id) const {
  CHECK(group_id);

  if (gids_.find(group) == gids_.end()) {
    LOG(ERROR) << "No group: " << group;
    return false;
  }

  *group_id = gids_.at(group);
  return true;
}

int64_t FakePlatform::AmountOfFreeDiskSpace(const base::FilePath& path) const {
  return real_platform_.AmountOfFreeDiskSpace(TestFilePath(path));
}

// Test API

void FakePlatform::SetUserId(const std::string& user, uid_t user_id) {
  CHECK(uids_.find(user) == uids_.end());

  uids_[user] = user_id;
}

void FakePlatform::SetGroupId(const std::string& group, gid_t group_id) {
  CHECK(gids_.find(group) == gids_.end());

  gids_[group] = group_id;
}

void FakePlatform::SetStandardUsersAndGroups() {
  SetUserId(fake_platform::kRoot, fake_platform::kRootUID);
  SetGroupId(fake_platform::kRoot, fake_platform::kRootGID);
  SetUserId(fake_platform::kChapsUser, fake_platform::kChapsUID);
  SetGroupId(fake_platform::kChapsUser, fake_platform::kChapsGID);
  SetUserId(fake_platform::kChronosUser, fake_platform::kChronosUID);
  SetGroupId(fake_platform::kChronosUser, fake_platform::kChronosGID);
  SetGroupId(fake_platform::kSharedGroup, fake_platform::kSharedGID);
}

void FakePlatform::SetSystemSaltForLibbrillo(const brillo::SecureBlob& salt) {
  std::string* brillo_salt = new std::string();
  brillo_salt->resize(salt.size());
  brillo_salt->assign(reinterpret_cast<const char*>(salt.data()), salt.size());
  brillo::cryptohome::home::SetSystemSalt(brillo_salt);
}

void FakePlatform::RemoveSystemSaltForLibbrillo() {
  std::string* salt = brillo::cryptohome::home::GetSystemSalt();
  brillo::cryptohome::home::SetSystemSalt(NULL);
  delete salt;
}

}  // namespace cryptohome
