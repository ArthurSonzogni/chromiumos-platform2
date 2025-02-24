// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TITLE_GENERATION_CACHE_STORAGE_H_
#define ODML_CORAL_TITLE_GENERATION_CACHE_STORAGE_H_

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <base/containers/lru_cache.h>
#include <base/files/file_path.h>

#include "odml/session_state_manager/session_state_manager.h"

namespace coral {

struct TitleCacheEntry {
  std::unordered_multiset<std::string> entity_titles;
  // The last update time that is used for expiration. It is the number of ms
  // since unix epoch.
  double last_updated;
};

class TitleCacheStorageInterface {
 public:
  virtual ~TitleCacheStorageInterface() = default;

  // Load the title cache for user's daemon store into title_cache.
  // Load() is guaranteed to clear the title_cache even in failure.
  virtual bool Load(
      const odml::SessionStateManagerInterface::User& user,
      base::HashingLRUCache<std::string, TitleCacheEntry>& title_cache) = 0;

  // Save the title_cache into user's daemon store.
  virtual bool Save(const odml::SessionStateManagerInterface::User& user,
                    const base::HashingLRUCache<std::string, TitleCacheEntry>&
                        title_cache) = 0;

  // Filter the cache for expired entries. Return true if modified.
  virtual bool FilterForExpiration(
      base::HashingLRUCache<std::string, TitleCacheEntry>& title_cache) = 0;
};

class TitleCacheStorage : public TitleCacheStorageInterface {
 public:
  // Specify an override for the base_path during testing. Use std::nullopt in
  // production.
  explicit TitleCacheStorage(std::optional<base::FilePath> base_path);

  bool Load(const odml::SessionStateManagerInterface::User& user,
            base::HashingLRUCache<std::string, TitleCacheEntry>& title_cache)
      override;

  bool Save(const odml::SessionStateManagerInterface::User& user,
            const base::HashingLRUCache<std::string, TitleCacheEntry>&
                title_cache) override;

  bool FilterForExpiration(base::HashingLRUCache<std::string, TitleCacheEntry>&
                               title_cache) override;

 private:
  // The base path to use when locating the storage file. Usually set to
  // std::nullopt for the default, but can be override for testing.
  std::optional<base::FilePath> base_path_;
};

}  // namespace coral

#endif  // ODML_CORAL_TITLE_GENERATION_CACHE_STORAGE_H_
