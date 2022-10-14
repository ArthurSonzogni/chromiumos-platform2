// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_USER_SESSION_MAP_H_
#define CRYPTOHOME_USER_SESSION_USER_SESSION_MAP_H_

#include <stddef.h>

#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "cryptohome/user_session/user_session.h"

namespace cryptohome {

// Container for storing user session objects.
// Must be used on single thread and sequence only.
class UserSessionMap final {
 private:
  // Declared here in the beginning to allow us to reference the underlying
  // storage type when defining the iterator.
  using Storage = std::map<std::string, std::unique_ptr<UserSession>>;

  // Iterator template that can act as both a regular and const iterator. This
  // wraps the underlying map iterator but exposes the underlying UserSession as
  // a UserSession& or const UserSession&, instead of as a reference to the
  // underlying unique_ptr<UserSession>.
  template <typename UserSessionType>
  class iterator_base {
   public:
    using value_type = std::pair<const std::string&, UserSessionType&>;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = Storage::difference_type;
    using pointer = value_type*;
    using reference = value_type&;

    iterator_base(const iterator_base& other) = default;
    iterator_base& operator=(const iterator_base& other) = default;

    iterator_base operator++(int) {
      iterator_base other(*this);
      ++(*this);
      return other;
    }

    iterator_base& operator++() {
      ++iter_;
      return *this;
    }

    value_type operator*() const {
      return value_type(iter_->first, *iter_->second);
    }

    bool operator==(const iterator_base& rhs) const {
      return iter_ == rhs.iter_;
    }
    bool operator!=(const iterator_base& rhs) const { return !(*this == rhs); }

   private:
    friend class UserSessionMap;
    explicit iterator_base(Storage::const_iterator iter) : iter_(iter) {}

    Storage::const_iterator iter_;
  };

 public:
  using iterator = iterator_base<UserSession>;
  using const_iterator = iterator_base<const UserSession>;

  UserSessionMap() = default;
  UserSessionMap(const UserSessionMap&) = delete;
  UserSessionMap& operator=(const UserSessionMap&) = delete;

  bool empty() const { return storage_.empty(); }
  size_t size() const { return storage_.size(); }

  iterator begin() { return iterator(storage_.begin()); }
  const_iterator begin() const { return const_iterator(storage_.begin()); }
  iterator end() { return iterator(storage_.end()); }
  const_iterator end() const { return const_iterator(storage_.end()); }

  // Adds the session for the given user. Returns false if the user already has
  // a session.
  bool Add(const std::string& account_id, std::unique_ptr<UserSession> session);
  // Removes the session for the given user. Returns false if there was no
  // session for the user.
  bool Remove(const std::string& account_id);
  // Returns a session for the given user, or null if there's none.
  UserSession* Find(const std::string& account_id);
  const UserSession* Find(const std::string& account_id) const;

 private:
  Storage storage_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_USER_SESSION_MAP_H_
