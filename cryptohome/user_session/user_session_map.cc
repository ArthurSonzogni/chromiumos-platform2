// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session/user_session_map.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/memory/scoped_refptr.h>

namespace cryptohome {

bool UserSessionMap::Add(const std::string& account_id,
                         std::unique_ptr<UserSession> session) {
  DCHECK(session);
  auto [unused_iter, was_inserted] =
      storage_.insert({account_id, std::move(session)});
  return was_inserted;
}

bool UserSessionMap::Remove(const std::string& account_id) {
  return storage_.erase(account_id) != 0;
}

UserSession* UserSessionMap::Find(const std::string& account_id) {
  auto iter = storage_.find(account_id);
  if (iter == storage_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

const UserSession* UserSessionMap::Find(const std::string& account_id) const {
  auto iter = storage_.find(account_id);
  if (iter == storage_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

}  // namespace cryptohome
