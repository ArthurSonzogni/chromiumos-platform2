// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_USER_SESSION_MAP_H_
#define CRYPTOHOME_USER_SESSION_USER_SESSION_MAP_H_

#include <stddef.h>

#include <map>
#include <string>

#include <base/memory/scoped_refptr.h>

#include "cryptohome/user_session/user_session.h"

namespace cryptohome {

// Container for storing user session objects.
// Must be used on single thread and sequence only.
class UserSessionMap final {
 private:
  // Declared here in the beginning to allow referring to the storage's
  // `iterator` in the public section.
  using Storage = std::map<std::string, scoped_refptr<UserSession>>;

 public:
  using iterator = Storage::iterator;

  UserSessionMap() = default;
  UserSessionMap(const UserSessionMap&) = delete;
  UserSessionMap& operator=(const UserSessionMap&) = delete;

  bool empty() const { return storage_.empty(); }
  size_t size() const { return storage_.size(); }

  // TODO(b/243846478): Add const iterators after getting rid of ref-counting in
  // `UserSession`.
  iterator begin() { return storage_.begin(); }
  iterator end() { return storage_.end(); }

  // Adds the session for the given user. Returns false if the user already has
  // a session.
  bool Add(const std::string& account_id, scoped_refptr<UserSession> session);
  // Removes the session for the given user. Returns false if there was no
  // session for the user.
  bool Remove(const std::string& account_id);
  // Returns a session for the given user, or null if there's none.
  // TODO(b/243846478): Add a const version after getting rid of ref-counting in
  // `UserSession`.
  scoped_refptr<UserSession> Find(const std::string& account_id);

 private:
  Storage storage_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_USER_SESSION_MAP_H_
