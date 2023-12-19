// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_UTILITY_CONCEPTS_H_
#define LIBHWSEC_FOUNDATION_UTILITY_CONCEPTS_H_

#include <concepts>
#include <type_traits>
#include <utility>

namespace hwsec_foundation {

template <typename T>
concept Dereferencable = requires(T t) { *t; };

// A helper to check an type has kMaxValue or not.
template <typename T>
concept EnumTypeWithMaxValue = std::is_enum_v<T>&&
  requires(T)
{
  { T::kMaxValue } -> std::same_as<T>;
};

template <typename T>
concept EnumTypeWithoutMaxValue = std::is_enum_v<T> && !EnumTypeWithMaxValue<T>;

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_UTILITY_CONCEPTS_H_
