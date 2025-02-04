// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/cache_storage.h"

#include <string>
#include <unordered_set>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/files/file_util.h>

#include "coral/proto_bindings/title_cache.pb.h"

namespace coral {

namespace {

// Files in /run/daemon-store-cache are prone to be cleaned up on low disk space
// situation.
// The full path of the embedding would be like
//   /run/daemon-store-cache/odmld/<user_hash>/coral/embeddings
// where the directory /run/daemon-store-cache/odmld/<user_hash> is
// automatically set up by the daemon store service on user login.
constexpr char kTitleCacheStorageRootDir[] = "/run/daemon-store-cache/odmld";

constexpr char kTitleCacheStorageSubDir[] = "coral";

constexpr char kTitleCacheStorageFileName[] = "title_cache";

base::FilePath GetTitleCacheRecordsPath(
    const std::optional<base::FilePath> base_path,
    const odml::SessionStateManagerInterface::User& user) {
  base::FilePath actual_base_path = base::FilePath(kTitleCacheStorageRootDir);
  if (base_path.has_value()) {
    actual_base_path = base_path.value();
  }
  return actual_base_path.Append(user.hash)
      .Append(kTitleCacheStorageSubDir)
      .Append(kTitleCacheStorageFileName);
}

void RecordsToCache(
    const TitleCacheRecords& records,
    base::HashingLRUCache<std::string, TitleCacheEntry>& title_cache) {
  for (const auto& record : records.records()) {
    std::unordered_multiset<std::string> entity_titles;
    for (const auto& entity_title : record.entity_titles()) {
      entity_titles.insert(entity_title);
    }
    title_cache.Put(record.cached_title(),
                    TitleCacheEntry{.entity_titles = std::move(entity_titles)});
  }
}

void CacheToRecords(
    const base::HashingLRUCache<std::string, TitleCacheEntry>& title_cache,
    TitleCacheRecords& records) {
  for (const auto& [title, title_cache_entry] : title_cache) {
    TitleCacheRecord* record = records.add_records();
    record->set_cached_title(title);
    for (const auto& entity : title_cache_entry.entity_titles) {
      record->add_entity_titles(entity);
    }
  }
}

}  // namespace

TitleCacheStorage::TitleCacheStorage(std::optional<base::FilePath> base_path)
    : base_path_(base_path) {}

bool TitleCacheStorage::Load(
    const odml::SessionStateManagerInterface::User& user,
    base::HashingLRUCache<std::string, TitleCacheEntry>& title_cache) {
  title_cache.Clear();

  base::FilePath file_path = GetTitleCacheRecordsPath(base_path_, user);
  if (!base::PathExists(file_path)) {
    // If the storage doesn't exist or is deleted, we'll assume it's empty.
    return true;
  }

  std::string buf;
  if (!base::ReadFileToString(file_path, &buf)) {
    LOG(WARNING) << "Failed to read the title cache storage.";
    return false;
  }

  TitleCacheRecords records;
  if (!records.ParseFromString(buf)) {
    LOG(ERROR) << "Failed to parse the title cache storage.";
    if (!brillo::DeleteFile(file_path)) {
      LOG(ERROR) << "Failed to delete the corrupt title cache storage.";
    }
    return false;
  }

  RecordsToCache(records, title_cache);
  return true;
}

bool TitleCacheStorage::Save(
    const odml::SessionStateManagerInterface::User& user,
    const base::HashingLRUCache<std::string, TitleCacheEntry>& title_cache) {
  base::FilePath file_path = GetTitleCacheRecordsPath(base_path_, user);
  if (!base::PathExists(file_path.DirName())) {
    base::File::Error error;
    if (!base::CreateDirectoryAndGetError(file_path.DirName(), &error)) {
      LOG(ERROR) << "Unable to create title cache storage directory: "
                 << base::File::ErrorToString(error);
      return false;
    } else {
      LOG(INFO) << "Created title cache storage directory.";
    }
  }

  TitleCacheRecords records;
  CacheToRecords(title_cache, records);

  std::string buf;
  if (!records.SerializeToString(&buf)) {
    LOG(ERROR) << "Failed to serialize the title cache.";
    return false;
  }
  if (!base::WriteFile(file_path, buf)) {
    LOG(ERROR) << "Failed to write the title cache to disk.";
    return false;
  }

  return true;
}

}  // namespace coral
