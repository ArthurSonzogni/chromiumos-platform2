// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuse/fuse.h>
#include <time.h>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/utf_string_conversions.h>
#include <base/synchronization/lock.h>
#include <brillo/syslog_logging.h>

#include "volume.h"  // NOLINT - TODO(b/32971714): fix it properly.

namespace {

const mode_t kFileMode = S_IRUSR | S_IRGRP | S_IFREG;
const mode_t kDirMode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IFDIR;

fat::Volume* g_volume = nullptr;

using DirectoryEntry = fat::Volume::DirectoryEntry;

// Uses base::Lock to use FileReader in a thread-safe manner.
class FileReaderThreadSafe {
 public:
  FileReaderThreadSafe(fat::Volume* volume,
                       int64_t start_cluster,
                       int64_t file_size)
      : reader_(volume, start_cluster, file_size) {}
  ~FileReaderThreadSafe() {}

  int64_t Read(char* buf, int64_t size, int64_t offset) {
    base::AutoLock auto_lock(lock_);
    return reader_.Read(buf, size, offset);
  }

 private:
  fat::Volume::FileReader reader_;
  base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(FileReaderThreadSafe);
};

// Converts DirectoryEntry to stat.
void ConvertDirectoryEntryToStat(const DirectoryEntry& entry,
                                 struct stat* stat) {
  if (entry.is_directory) {
    stat->st_mode = kDirMode;
    stat->st_nlink = 2;
  } else {
    stat->st_mode = kFileMode;
    stat->st_nlink = 1;
    stat->st_size = entry.file_size;
  }
  stat->st_mtime = entry.last_modification.ToBaseTime().ToTimeT();
}

// Gets a DirectoryEntry with the given path.
bool GetDirectoryEntry(const base::StringPiece16& path, DirectoryEntry* out) {
  if (path.empty() || path[0] != '/') {
    return false;
  }
  int64_t current_directory_start_sector = g_volume->root_dir_start_sector();
  size_t pos = 1;
  while (true) {
    size_t next_slash = path.find('/', pos);
    if (next_slash == base::StringPiece::npos) {
      next_slash = path.size();
    }
    base::StringPiece16 name(path.data() + pos, next_slash - pos);
    DirectoryEntry entry;
    bool found = false;
    auto entry_finder = [&name, &entry, &found](
                            const base::StringPiece16& name_in,
                            const DirectoryEntry& entry_in) {
      // TODO(hashimoto): Consider using base::i18n::ToLower to be
      // case-insensitive for non-ASCII characters.
      if (base::EqualsCaseInsensitiveASCII(name, name_in)) {
        entry = entry_in;
        found = true;
        return false;
      }
      return true;
    };
    if (!g_volume->ReadDirectory(current_directory_start_sector,
                                 base::Bind(&decltype(entry_finder)::operator(),
                                            base::Unretained(&entry_finder))) ||
        !found) {
      return false;
    }
    pos = next_slash + 1;
    if (pos >= path.size()) {
      *out = entry;
      return true;
    }
    if (!entry.is_directory) {
      return false;
    }
    current_directory_start_sector =
        g_volume->GetClusterStartSector(entry.start_cluster);
  }
}

int fat_getattr(const char* path, struct stat* stat) {
  if (strcmp(path, "/") == 0) {
    stat->st_mode = kDirMode;
    stat->st_nlink = 2;
    return 0;
  }
  DirectoryEntry entry;
  if (!GetDirectoryEntry(base::UTF8ToUTF16(path), &entry)) {
    return -ENOENT;
  }
  ConvertDirectoryEntryToStat(entry, stat);
  return 0;
}

int fat_open(const char* path, struct fuse_file_info* fi) {
  if ((fi->flags & O_ACCMODE) != O_RDONLY) {
    return -EACCES;
  }
  DirectoryEntry entry;
  if (!GetDirectoryEntry(base::UTF8ToUTF16(path), &entry)) {
    return -ENOENT;
  }
  if (entry.is_directory) {
    return -EISDIR;
  }
  fi->keep_cache = 1;
  fi->fh = reinterpret_cast<uint64_t>(
      new FileReaderThreadSafe(g_volume, entry.start_cluster, entry.file_size));
  return 0;
}

int fat_read(const char* path,
             char* buf,
             size_t size,
             off_t off,
             struct fuse_file_info* fi) {
  int64_t result =
      reinterpret_cast<FileReaderThreadSafe*>(fi->fh)->Read(buf, size, off);
  if (result < 0) {
    return -EIO;
  }
  return result;
}

int fat_release(const char* path, struct fuse_file_info* fi) {
  delete reinterpret_cast<FileReaderThreadSafe*>(fi->fh);
  return 0;
}

int fat_readdir(const char* path,
                void* buf,
                fuse_fill_dir_t filler,
                off_t offset,
                struct fuse_file_info* fi) {
  filler(buf, ".", nullptr, 0);
  filler(buf, "..", nullptr, 0);
  int64_t start_sector = 0;
  if (strcmp(path, "/") == 0) {
    start_sector = g_volume->root_dir_start_sector();
  } else {
    DirectoryEntry entry;
    if (!GetDirectoryEntry(base::UTF8ToUTF16(path), &entry)) {
      return -ENOENT;
    }
    if (!entry.is_directory) {
      return -ENOTDIR;
    }
    start_sector = g_volume->GetClusterStartSector(entry.start_cluster);
  }
  auto filler_adaptor = [&buf, &filler](const base::StringPiece16& name,
                                        const DirectoryEntry& entry) {
    filler(buf, base::UTF16ToUTF8(name).c_str(), nullptr, 0);
    return true;
  };
  if (!g_volume->ReadDirectory(start_sector,
                               base::Bind(&decltype(filler_adaptor)::operator(),
                                          base::Unretained(&filler_adaptor)))) {
    return -EIO;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  if (argc != 5) {
    LOG(ERROR) << "Usage: " << argv[0]
               << " obb_filename mount_path owner_uid owner_gid";
    return 1;
  }
  const char* file_system_name = argv[0];
  const char* obb_filename = argv[1];
  const char* mount_path = argv[2];
  const char* owner_uid = argv[3];
  const char* owner_gid = argv[4];

  base::File file(base::FilePath(obb_filename),
                  base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to open: " << obb_filename;
    return 1;
  }
  fat::Volume volume;
  if (!volume.Initialize(std::move(file))) {
    LOG(ERROR) << "Failed to initialize volume: " << obb_filename;
    return 1;
  }
  g_volume = &volume;

  const std::string mount_options =
      std::string("allow_other,default_permissions,uid=") + owner_uid +
      ",gid=" + owner_gid;
  const char* fuse_argv[] = {
      file_system_name, mount_path, "-f", "-o", mount_options.c_str(),
  };
  struct fuse_operations fat_ops = {};
#define SET_FAT_OP(name) fat_ops.name = fat_##name
  SET_FAT_OP(getattr);
  SET_FAT_OP(open);
  SET_FAT_OP(read);
  SET_FAT_OP(release);
  SET_FAT_OP(readdir);
#undef SET_FAT_OP
  fuse_main(arraysize(fuse_argv), const_cast<char**>(fuse_argv), &fat_ops,
            nullptr);
  return 0;
}
