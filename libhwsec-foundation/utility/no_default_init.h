// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_UTILITY_NO_DEFAULT_INIT_H_
#define LIBHWSEC_FOUNDATION_UTILITY_NO_DEFAULT_INIT_H_

#include <utility>

// A helper to remove the default constructor for objects.
//
// Example usage:
//
// struct StructName {
//   NoDefault<int> val;
//   NoDefault<std::string> str;
// };
//
// These would prevent the following code being compileable:
// StructName{};
// StructName{.val = 123};
// StructName{.str = "456"};
//
// But the following code is still compileable:
// StructName{.val = 123, .str = "456"};

namespace hwsec_foundation {

template <typename T, typename = void>
class NoDefault : public T {
 public:
  using T::T;
  using T::operator=;

  NoDefault(T&& t) : T(std::move(t)) {}  // NOLINT(runtime/explicit)
  NoDefault(const T& t) : T(t) {}        // NOLINT(runtime/explicit)
  NoDefault() = delete;

  ~NoDefault() = default;
};

template <typename T>
class NoDefault<T, std::enable_if_t<std::is_scalar_v<T>>> {
 public:
  NoDefault(T v) : value_(v) {}  // NOLINT(runtime/explicit)
  NoDefault() = delete;

  ~NoDefault() = default;

  operator T() const { return value_; }

 private:
  T value_;
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_UTILITY_NO_DEFAULT_INIT_H_
