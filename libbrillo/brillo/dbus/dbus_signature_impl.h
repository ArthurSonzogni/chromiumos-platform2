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

namespace google {
namespace protobuf {
class MessageLite;
}  // namespace protobuf
}  // namespace google

namespace brillo::dbus_utils::internal {

// Equivalent to std::remove_cvref in c++20.
template <typename T>
struct RemoveCvref {
  using Type = std::remove_cv_t<std::remove_reference_t<T>>;
};

// Equivalent to std::remove_cvref_t in c++20.
template <typename T>
using RemoveCvrefT = typename RemoveCvref<T>::Type;

// Equilvalent to std::to_array in c++20.
template <typename T, std::size_t N, std::size_t... Is>
constexpr std::array<std::remove_cv_t<T>, N> ToArrayImpl(
    T (&data)[N], std::index_sequence<Is...>) {
  return {{data[Is]...}};
}

template <typename T, std::size_t N>
constexpr std::array<std::remove_cv_t<T>, N> ToArray(T (&data)[N]) {
  return ToArrayImpl(data, std::make_index_sequence<N>());
}

// Additional overload for std::array<> to map itself.
template <typename T, std::size_t N>
constexpr std::array<T, N> ToArray(const std::array<T, N>& array) {
  return array;
}

// Checks whether given type is ok to be used as an argument of StrJoin.
// This is just to make the error message more readable.
template <typename T>
struct IsStrJoinable : std::false_type {};
template <size_t N>
struct IsStrJoinable<char[N]> : std::true_type {};
template <size_t N>
struct IsStrJoinable<std::array<char, N>> : std::true_type {};

// Implementaion of StrJoin.
template <std::size_t... Ns>
constexpr auto StrJoinImpl(std::array<char, Ns>... args) {
  constexpr std::size_t kSize = (Ns + ...) - sizeof...(args) + 1;
  std::array<char, kSize> result = {};
  std::size_t i = 0;
  for (auto span : {base::span<char>(args)...}) {
    for (std::size_t j = 0; j < span.size(); ++j) {
      result[i + j] = span[j];
    }
    i += span.size() - 1;  // Excluding trailing '\0'.
  }
  return result;
}

// StrJoin takes char[N]s and std::array<char, M>s, and returns the
// std::array<char, ...> which is the result of concat.
template <typename... Ts>
constexpr auto StrJoin(Ts&&... args) {
  static_assert((IsStrJoinable<RemoveCvrefT<Ts>>::value && ...),
                "All types passed to StrJoin must be either char[N] or "
                "std::array<char, N>.");
  return StrJoinImpl(ToArray(std::forward<Ts>(args))...);
}

// std::true_type if the given T is a protobuf type, i.e. its base class is
// google::protobuf::MessageLite.
template <typename T>
using IsProtobuf = std::is_base_of<google::protobuf::MessageLite, T>;

}  // namespace brillo::dbus_utils::internal

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_SIGNATURE_IMPL_H_
