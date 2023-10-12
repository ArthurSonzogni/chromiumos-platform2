// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/pinweaver_manager/persistent_lookup_table.h"

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/secure_blob.h>

namespace {

// Helper function to create a file path, given a key directory
// |key_dir| and a version number of the file, |version|.
base::FilePath CreateFilePathForKey(const base::FilePath& key_dir,
                                    uint32_t version) {
  return key_dir.Append(std::to_string(version)).AddExtension("value");
}

bool ReadFileToBlob(const base::FilePath& path, brillo::Blob* blob) {
  int64_t file_size;
  if (!base::GetFileSize(path, &file_size)) {
    LOG(ERROR) << "Could not get size of " << path.value();
    return false;
  }
  blob->resize(file_size);
  int data_read =
      base::ReadFile(path, reinterpret_cast<char*>(blob->data()), blob->size());

  return data_read == static_cast<int>(file_size);
}

bool TouchFileDurable(const base::FilePath& path) {
  return base::WriteFile(path, "", 0) == 0;
}

}  // namespace

namespace hwsec {

PersistentLookupTable::PersistentLookupTable(base::FilePath basedir)
    : table_dir_(basedir) {}

PLTError PersistentLookupTable::GetValue(const uint64_t key,
                                         std::vector<uint8_t>& value) {
  uint32_t latest_version = FindLatestVersion(key);

  if (latest_version == 0) {
    VLOG(1) << "No entry exists for this key: " << key;
    return PLT_KEY_NOT_FOUND;
  }

  base::FilePath key_dir = table_dir_.Append(std::to_string(key));
  base::FilePath filepath = CreateFilePathForKey(key_dir, latest_version);
  if (!ReadFileToBlob(filepath, &value)) {
    LOG(ERROR) << "Trouble reading file: " << filepath.value();
    return PLT_STORAGE_ERROR;
  }

  // If the key directory has been marked as deleted, we should
  // return a |false|.
  if (value.size() == 0) {
    value.clear();
    return PLT_KEY_NOT_FOUND;
  }

  return PLT_SUCCESS;
}

PLTError PersistentLookupTable::StoreValue(
    const uint64_t key, const std::vector<uint8_t>& new_val) {
  uint32_t latest_version = FindLatestVersion(key);
  base::FilePath key_dir = table_dir_.Append(std::to_string(key));

  // Key doesn't exist.
  if (latest_version == 0) {
    if (!base::CreateDirectory(key_dir)) {
      PLOG(ERROR) << "Failed to create key dir: " << key_dir.value();
      return PLT_STORAGE_ERROR;
    }
  }

  // Create new file version.
  uint32_t new_version = latest_version + 1;
  CHECK(new_version);
  base::FilePath new_file = CreateFilePathForKey(key_dir, new_version);

  if (!brillo::WriteBlobToFileAtomic<brillo::Blob>(new_file, new_val, 0644)) {
    LOG(ERROR) << "Failed to create disk entry for file: " << new_file.value();
    return PLT_STORAGE_ERROR;
  }

  if (!brillo::SyncFileOrDirectory(key_dir, /*is_directory=*/true,
                                   /*data_sync*/ false)) {
    LOG(ERROR) << "Failed to sync key dir: " << key_dir.value();
    return PLT_STORAGE_ERROR;
  }

  return PLT_SUCCESS;
}

PLTError PersistentLookupTable::RemoveKey(const uint64_t key) {
  uint32_t latest_version = FindLatestVersion(key);

  if (latest_version != 0) {
    // Create new file version.
    uint32_t new_version = latest_version + 1;
    CHECK(new_version);
    base::FilePath key_dir = table_dir_.Append(std::to_string(key));
    base::FilePath new_file = CreateFilePathForKey(key_dir, new_version);

    if (!TouchFileDurable(new_file)) {
      LOG(ERROR) << "Failed to create disk entry for file: "
                 << new_file.value();
      // If we couldn't write the "bad" file, something is amiss and we
      // should surface an error.
      return PLT_STORAGE_ERROR;
    }
  }

  // Delete the entire directory anyway.
  DeleteOldKeyVersions(key, 0);
  return PLT_SUCCESS;
}

bool PersistentLookupTable::KeyExists(const uint64_t key) {
  return FindLatestVersion(key) != 0;
}

void PersistentLookupTable::GetUsedKeys(std::vector<uint64_t>& key_list) {
  // Go through all key directories, and if there are valid key entries,
  // add it to the list.
  base::FileEnumerator file(table_dir_, false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath cur_dir = file.Next(); !cur_dir.empty();
       cur_dir = file.Next()) {
    uint64_t key;
    if (!base::StringToUint64(cur_dir.BaseName().value(), &key)) {
      LOG(WARNING) << "Can't parse directory, skipping: " << cur_dir.value();
      continue;
    }
    if (KeyExists(key)) {
      key_list.push_back(key);
    }
  }
}

bool PersistentLookupTable::Init() {
  if (!base::DirectoryExists(table_dir_)) {
    VLOG(1) << "Lookup table dir not found, have to create it.";
    if (!base::CreateDirectory(table_dir_)) {
      PLOG(ERROR) << "Failed to create dir: " << table_dir_.value();
      return false;
    }
  } else {
    // Remove all old key versions of all keys.
    base::FileEnumerator file(table_dir_, false,
                              base::FileEnumerator::DIRECTORIES);
    for (base::FilePath cur_dir = file.Next(); !cur_dir.empty();
         cur_dir = file.Next()) {
      uint64_t key;
      if (!base::StringToUint64(cur_dir.BaseName().value(), &key)) {
        LOG(WARNING) << "Can't parse directory, skipping: " << cur_dir.value();
        continue;
      }
      uint32_t version = FindLatestVersion(key);
      DeleteOldKeyVersions(key, version);
    }
  }
  return true;
}

uint32_t PersistentLookupTable::FindLatestVersion(const uint64_t key) {
  base::FilePath key_dir = table_dir_.Append(std::to_string(key));
  if (!base::DirectoryExists(key_dir)) {
    // No directory with this key, so return 0;
    return 0;
  }

  base::FileEnumerator file(key_dir, false, base::FileEnumerator::FILES,
                            "*.value");
  uint32_t latest_version = 0;
  for (base::FilePath cur_file = file.Next(); !cur_file.empty();
       cur_file = file.Next()) {
    uint32_t cur_version;
    // Get the version number.
    if (!base::StringToUint(cur_file.BaseName().RemoveExtension().value(),
                            &cur_version)) {
      // If the file name is corrupt, we should just skip it.
      LOG(ERROR) << "File name is not of correct format." << cur_file.value();
      continue;
    }

    if (cur_version > latest_version) {
      // TODO(pmalani): Make sure the data in this file is verified.
      latest_version = cur_version;
    }
  }

  return latest_version;
}

void PersistentLookupTable::DeleteOldKeyVersions(const uint64_t key,
                                                 uint32_t version_to_save) {
  base::FilePath key_dir = table_dir_.Append(std::to_string(key));
  if (!base::DirectoryExists(key_dir)) {
    return;
  }

  // Delete the entire directory.
  if (version_to_save == 0) {
    if (!brillo::DeletePathRecursively(key_dir)) {
      LOG(WARNING) << "Failed to delete dir: " << key_dir.value();
    }
    return;
  }

  base::FileEnumerator file(key_dir, false, base::FileEnumerator::FILES);
  base::FilePath cur_file;
  while (true) {
    cur_file = file.Next();
    if (cur_file.empty()) {
      break;
    }

    // Ignore file for version |version_to_save|.
    if (cur_file.BaseName().RemoveExtension().value() ==
        std::to_string(version_to_save)) {
      continue;
    }

    if (!brillo::DeleteFile(cur_file)) {
      LOG(WARNING) << "Failed to delete file: " << cur_file.value();
    }
  }
}

}  // namespace hwsec
