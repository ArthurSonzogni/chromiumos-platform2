// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "user_oldest_activity_timestamp_cache.h"

#include <base/logging.h>

using base::FilePath;

namespace cryptohome {

void UserOldestActivityTimestampCache::Initialize() {
  CHECK(initialized_ == false);
  initialized_ = true;
}

void UserOldestActivityTimestampCache::AddExistingUser(
    const FilePath& vault, base::Time timestamp) {
  CHECK(initialized_);
  users_timestamp_.insert(std::make_pair(timestamp, vault));
  if (oldest_known_timestamp_ > timestamp ||
      oldest_known_timestamp_.is_null()) {
    oldest_known_timestamp_ = timestamp;
  }
}

void UserOldestActivityTimestampCache::UpdateExistingUser(
    const FilePath& vault, base::Time timestamp) {
  CHECK(initialized_);
  for (UsersTimestamp::iterator i = users_timestamp_.begin();
       i != users_timestamp_.end(); ++i) {
    if (i->second == vault) {
      base::Time timestamp = users_timestamp_.begin()->first;
      users_timestamp_.erase(i);
      UpdateTimestampAfterRemoval(timestamp);
      break;
    }
  }
  AddExistingUser(vault, timestamp);
}

void UserOldestActivityTimestampCache::AddExistingUserNotime(
    const FilePath& vault) {
  CHECK(initialized_);
  users_timestamp_.insert(std::make_pair(base::Time(), vault));
}

FilePath UserOldestActivityTimestampCache::RemoveOldestUser() {
  CHECK(initialized_);
  FilePath vault;
  if (!users_timestamp_.empty()) {
    vault = users_timestamp_.begin()->second;
    base::Time timestamp = users_timestamp_.begin()->first;
    users_timestamp_.erase(users_timestamp_.begin());
    UpdateTimestampAfterRemoval(timestamp);
  }
  return vault;
}

void UserOldestActivityTimestampCache::UpdateTimestampAfterRemoval(
    base::Time timestamp) {
  if (oldest_known_timestamp_ == timestamp) {
    oldest_known_timestamp_ = users_timestamp_.empty() ?
        base::Time() : users_timestamp_.begin()->first;
  }
}


}  // namespace cryptohome
