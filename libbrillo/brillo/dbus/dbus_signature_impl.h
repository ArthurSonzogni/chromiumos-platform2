// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DBUS_DBUS_SIGNATURE_IMPL_H_
#define LIBBRILLO_BRILLO_DBUS_DBUS_SIGNATURE_IMPL_H_

// Contains utilities to implement DBusSignature.

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <base/containers/span.h>

namespace brillo::dbus_utils::internal {

// StrJoin takes char[N]s and std::array<char, M>s, each of which is null
// terminated string, and returns the std::array<char, ...> which is the
// result of concat, also null terminated.
template <typename T>
struct IsStrJoinable : std::false_type {};
template <std::size_t N>
struct IsStrJoinable<char[N]> : std::true_type {};
template <std::size_t N>
struct IsStrJoinable<std::array<char, N>> : std::true_type {};

template <typename... Ts>
constexpr auto StrJoin(Ts&&... args) {
  static_assert((IsStrJoinable<std::remove_cvref_t<Ts>>::value && ...),
                "All types passed to StrJoin must be either char[N] or "
                "std::array<char, N>.");

  constexpr std::size_t kSize =
      (std::size(std::remove_cvref_t<Ts>{}) + ...) - sizeof...(args) + 1;
  std::array<char, kSize> result = {};
  std::size_t i = 0;
  for (auto span : {base::span<const char>(args)...}) {
    for (std::size_t j = 0; j < span.size(); ++j) {
      result[i + j] = span[j];
    }
    i += span.size() - 1;  // Excluding trailing '\0'.
  }
  return result;
}

}  // namespace brillo::dbus_utils::internal

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_SIGNATURE_IMPL_H_
