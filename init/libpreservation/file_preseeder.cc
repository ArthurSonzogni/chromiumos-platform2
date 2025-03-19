// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/free_deleter.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/file_utils.h>
#include <init/libpreservation/file_preseeder.h>
#include <init/libpreservation/filesystem_manager.h>
#include <init/libpreservation/preseeded_files.pb.h>

namespace libpreservation {
namespace {

// A container for fiemap that handles cleaning up allocated memory.
// base::FreeDeleter must be used here because the underlying struct fiemap is
// allocated with malloc().
typedef std::unique_ptr<struct fiemap, base::FreeDeleter> ScopedFiemap;

// ext4 limit for inline file sizes.
constexpr size_t kInlineFileSizeLimit = 256;

// Max extent count.
constexpr int kMaxExtents = 128;

// Block size.
constexpr int kBlockSize = 4096;

}  // namespace

FilePreseeder::FilePreseeder(
    const std::set<base::FilePath>& directory_allowlist,
    const base::FilePath& fs_root,
    const base::FilePath& mount_root,
    const base::FilePath& metadata_path)
    : directory_allowlist_(directory_allowlist),
      fs_root_(fs_root),
      mount_root_(mount_root),
      metadata_path_(metadata_path) {}

FilePreseeder::~FilePreseeder() {}

bool FilePreseeder::SaveFileState(const std::set<base::FilePath>& file_list) {
  bool ret = true;
  for (auto preseeded_file : file_list) {
    base::FilePath file = mount_root_.Append(preseeded_file);
    if (!base::PathExists(file)) {
      continue;
    }

    auto file_size = base::GetFileSize(file);
    if (!file_size) {
      LOG(ERROR) << "Failed to get file size for " << file;
      continue;
    }
    std::string data;

    if (*file_size < kInlineFileSizeLimit && *file_size != 0) {
      if (!base::ReadFileToString(file, &data)) {
        PLOG(ERROR) << "Failed to read config from " << file;
        continue;
      }
    }

    PreseededFile* pfile = preseeded_files_.add_file_list();
    pfile->set_path(preseeded_file.value());
    pfile->set_size(*file_size);

    if (*file_size < kInlineFileSizeLimit) {
      pfile->mutable_contents()->set_data(data);
    } else if (*file_size != 0) {
      if (!GetFileExtents(file, pfile->mutable_contents()->mutable_extents())) {
        LOG(ERROR) << "Failed to get extents for " << preseeded_file;
        ret = false;
        continue;
      }
    }
  }

  return ret && PersistMetadata();
}

bool FilePreseeder::PersistMetadata() {
  std::string serialized = preseeded_files_.SerializeAsString();
  auto base64_encoded = base::Base64Encode(serialized);

  return brillo::WriteToFileAtomic(metadata_path_, base64_encoded.c_str(),
                                   base64_encoded.size(), 0644);
}

bool FilePreseeder::LoadMetadata() {
  std::string base64_data;
  if (!base::ReadFileToString(metadata_path_, &base64_data)) {
    return false;
  }

  std::string decoded_pb;
  if (!base::Base64Decode(base64_data, &decoded_pb)) {
    LOG(ERROR) << "Failed to base64 decode protobuf";
    return false;
  }

  if (!preseeded_files_.ParseFromString(decoded_pb)) {
    LOG(ERROR) << "Failed to parse protobuf";
    return false;
  }

  return true;
}

bool FilePreseeder::CreateDirectoryRecursively(FilesystemManager* fs_manager,
                                               const base::FilePath& path) {
  base::FilePath dir = fs_root_;
  std::vector<std::string> components = path.GetComponents();

  if (fs_manager->FileExists(fs_root_.Append(path))) {
    return true;
  }

  // Start with first non-root component.
  for (auto& comp : components) {
    dir = dir.Append(comp);

    if (fs_manager->FileExists(dir)) {
      continue;
    }

    if (!fs_manager->CreateDirectory(dir)) {
      LOG(ERROR) << "Failed to restore directory: " << dir;
      return false;
    }
  }
  return true;
}

bool FilePreseeder::CheckAllowlist(const base::FilePath& path) {
  // Check if file is in the directory allowlist.
  base::FilePath dir;
  std::vector<std::string> components = path.GetComponents();
  for (auto& comp : components) {
    dir = dir.empty() ? base::FilePath(comp) : dir.Append(comp);
    if (directory_allowlist_.find(dir) != directory_allowlist_.end()) {
      return true;
    }
  }

  return false;
}

bool FilePreseeder::RestoreExtentFiles(FilesystemManager* fs_manager) {
  for (auto& file : preseeded_files_.file_list()) {
    if (!CheckAllowlist(base::FilePath(file.path()))) {
      LOG(ERROR) << "Skipping file: " << file.path() << "; not in allowlist";
      continue;
    }

    // Skip files with no contents.
    if (!file.has_contents()) {
      continue;
    }

    // Skip small files.
    if (file.contents().has_data() || !file.contents().has_extents()) {
      continue;
    }

    base::FilePath pfile = fs_root_.Append(file.path());
    if (fs_manager->FileExists(pfile)) {
      continue;
    }

    base::FilePath parent_dir = base::FilePath(file.path()).DirName();

    if (!fs_manager->FileExists(fs_root_.Append(parent_dir)) &&
        !CreateDirectoryRecursively(fs_manager, parent_dir)) {
      LOG(ERROR) << "Failed to create directory: "
                 << fs_root_.Append(parent_dir);
      return false;
    }

    if (!fs_manager->CreateFileAndFixedGoalFallocate(
            pfile, file.size(), file.contents().extents())) {
      fs_manager->UnlinkFile(pfile);
    }
  }

  return true;
}

bool FilePreseeder::RestoreInlineFiles() {
  for (auto& file : preseeded_files_.file_list()) {
    if (!CheckAllowlist(base::FilePath(file.path()))) {
      LOG(ERROR) << "Skipping file: " << file.path() << "; not in allowlist";
      continue;
    }

    // Skip files with no contents.
    if (!file.has_contents()) {
      continue;
    }

    // Skip extent files.
    if (file.contents().has_extents() || !file.contents().has_data()) {
      continue;
    }

    base::FilePath path = mount_root_.Append(file.path());
    base::FilePath parent_dir = path.DirName();
    if (!base::PathExists(parent_dir)) {
      if (!base::CreateDirectory(parent_dir)) {
        LOG(ERROR) << "Failed to create directory: " << parent_dir;
        return false;
      }
    }

    std::string contents = file.size() != 0 ? file.contents().data() : "";
    if (!base::WriteFile(path, contents)) {
      LOG(ERROR) << "Failed to create file: " << path;
    }
  }

  return true;
}

bool FilePreseeder::RestoreRootFlagFiles(
    const std::set<base::FilePath>& file_allowlist) {
  bool ret = true;
  for (auto& file : preseeded_files_.file_list()) {
    base::FilePath root_file = base::FilePath(file.path());
    if (file_allowlist.find(root_file) == file_allowlist.end()) {
      continue;
    }

    if (!base::WriteFile(mount_root_.Append(root_file), "")) {
      LOG(ERROR) << "Failed to create file" << root_file;
      ret = false;
      continue;
    }
  }

  return ret;
}

bool FilePreseeder::GetFileExtents(const base::FilePath& path,
                                   ExtentArray* extents) {
  base::ScopedFD fd(open(path.value().c_str(), O_RDONLY | O_CLOEXEC));
  if (fd.get() < 0) {
    PLOG(ERROR) << "Unable to open file: " << path.value();
    return false;
  }

  size_t alloc_size = offsetof(struct fiemap, fm_extents[kMaxExtents]);
  ScopedFiemap fm(static_cast<struct fiemap*>(malloc(alloc_size)));
  memset(fm.get(), 0, alloc_size);

  while (fm->fm_mapped_extents <= kMaxExtents) {
    bool last_extent_found = false;

    fm->fm_length = std::numeric_limits<uint64_t>::max();
    fm->fm_flags |= FIEMAP_FLAG_SYNC;
    fm->fm_extent_count = kMaxExtents;

    if (HANDLE_EINTR(ioctl(fd.get(), FS_IOC_FIEMAP, fm.get())) < -1) {
      PLOG(ERROR) << "Unable to get FIEMAP for file: " << path.value();
      return false;
    }

    if (fm->fm_mapped_extents < 0 || fm->fm_mapped_extents > kMaxExtents) {
      LOG(ERROR) << "Invalid extent count " << fm->fm_mapped_extents << "for "
                 << "path " << path;
      return false;
    }

    for (uint32_t i = 0; i < fm->fm_mapped_extents; i++) {
      Extent* e = extents->add_extent();
      e->set_goal(fm->fm_extents[i].fe_physical / kBlockSize);
      e->set_start(fm->fm_extents[i].fe_logical / kBlockSize);
      e->set_length(fm->fm_extents[i].fe_length / kBlockSize);

      if (i == fm->fm_mapped_extents - 1) {
        fm->fm_start =
            fm->fm_extents[i].fe_logical + fm->fm_extents[i].fe_length;
        if (fm->fm_extents[i].fe_flags & FIEMAP_EXTENT_LAST) {
          last_extent_found = true;
        }
      }
    }

    if (last_extent_found) {
      break;
    }
  }

  return true;
}

}  // namespace libpreservation
