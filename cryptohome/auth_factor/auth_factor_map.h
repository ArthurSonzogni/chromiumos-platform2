// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MAP_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MAP_H_

#include <map>
#include <memory>
#include <string>

#include "cryptohome/auth_factor/auth_factor.h"

namespace cryptohome {

// Container for storing AuthFactor instances loaded from storage.
// Must be use on single thread and sequence only.
class AuthFactorMap final {
 private:
  // Declared here in the beginning to allow us to reference the underlying
  // storage type when defining the iterator.
  using Storage = std::map<std::string, std::unique_ptr<AuthFactor>>;

  // Iterator template that can act as both a regular and const iterator. This
  // wraps the underlying map iterator but exposes the underlying UserSession as
  // a AuthFactor& or const AuthFactor&, instead of as a reference to the
  // underlying unique_ptr<AuthFactor>.
  template <typename T>
  class iterator_base {
   public:
    using value_type = T;
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

    value_type& operator*() const { return *iter_->second; }

    bool operator==(const iterator_base& rhs) const {
      return iter_ == rhs.iter_;
    }
    bool operator!=(const iterator_base& rhs) const { return !(*this == rhs); }

   private:
    friend class AuthFactorMap;
    explicit iterator_base(Storage::const_iterator iter) : iter_(iter) {}

    Storage::const_iterator iter_;
  };

 public:
  using iterator = iterator_base<AuthFactor>;
  using const_iterator = iterator_base<const AuthFactor>;

  AuthFactorMap() = default;
  AuthFactorMap(const AuthFactorMap&) = delete;
  AuthFactorMap& operator=(const AuthFactorMap&) = delete;

  bool empty() const { return storage_.empty(); }
  size_t size() const { return storage_.size(); }

  iterator begin() { return iterator(storage_.begin()); }
  const_iterator begin() const { return const_iterator(storage_.begin()); }
  iterator end() { return iterator(storage_.end()); }
  const_iterator end() const { return const_iterator(storage_.end()); }

  // Add a factor to the map. The factors are only stored by label and so adding
  // a new factor with the same label will overwrite the prior one.
  void Add(std::unique_ptr<AuthFactor> auth_factor);
  // Removes the factor for a given label. Does nothing if there is no factor
  // with that label.
  void Remove(const std::string& label);
  // Returns a pointer the factor for a given label, or null if there's none.
  AuthFactor* Find(const std::string& label);
  const AuthFactor* Find(const std::string& label) const;

 private:
  Storage storage_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MAP_H_
