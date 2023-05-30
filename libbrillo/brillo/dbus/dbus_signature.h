// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DBUS_DBUS_SIGNATURE_H_
#define LIBBRILLO_BRILLO_DBUS_DBUS_SIGNATURE_H_

// There are a number of overloads to handle C++ equivalents of basic D-Bus
// types:
//   D-Bus Type  | D-Bus Signature | Native C++ type
//  --------------------------------------------------
//   BYTE        |        y        |  uint8_t
//   BOOL        |        b        |  bool
//   INT16       |        n        |  int16_t
//   UINT16      |        q        |  uint16_t
//   INT32       |        i        |  int32_t (int)
//   UINT32      |        u        |  uint32_t (unsigned)
//   INT64       |        x        |  int64_t
//   UINT64      |        t        |  uint64_t
//   DOUBLE      |        d        |  double
//   STRING      |        s        |  std::string
//   OBJECT_PATH |        o        |  dbus::ObjectPath
//   ARRAY       |        aT       |  std::vector<T>
//   STRUCT      |       (UV)      |  std::pair<U,V>
//               |     (UVW...)    |  std::tuple<U,V,W,...>
//   DICT        |       a{KV}     |  std::map<K,V>
//   VARIANT     |        v        |  brillo::Any
//   UNIX_FD     |        h        |  base::ScopedFD
//   SIGNATURE   |        g        |  (unsupported)

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>

#include "brillo/dbus/dbus_signature_impl.h"

namespace brillo {
class Any;
}  // namespace brillo

namespace dbus {
class ObjectPath;
}  // namespace dbus

namespace brillo::dbus_utils {

// Provides DBusSignature::kValue, which is typed std::array<char, N>,
// holding the D-Bus signature.
template <typename T, typename = void>
struct DBusSignature {
  static_assert(false, "Unsupported type for D-Bus");
};

template <>
struct DBusSignature<std::uint8_t> {
  static constexpr auto kValue = internal::ToArray("y");
};

template <>
struct DBusSignature<bool> {
  static constexpr auto kValue = internal::ToArray("b");
};

template <>
struct DBusSignature<std::int16_t> {
  static constexpr auto kValue = internal::ToArray("n");
};

template <>
struct DBusSignature<std::uint16_t> {
  static constexpr auto kValue = internal::ToArray("q");
};

template <>
struct DBusSignature<std::int32_t> {
  static constexpr auto kValue = internal::ToArray("i");
};

template <>
struct DBusSignature<std::uint32_t> {
  static constexpr auto kValue = internal::ToArray("u");
};

template <>
struct DBusSignature<std::int64_t> {
  static constexpr auto kValue = internal::ToArray("x");
};

template <>
struct DBusSignature<std::uint64_t> {
  static constexpr auto kValue = internal::ToArray("t");
};

template <>
struct DBusSignature<double> {
  static constexpr auto kValue = internal::ToArray("d");
};

template <>
struct DBusSignature<std::string> {
  static constexpr auto kValue = internal::ToArray("s");
};

// Overloading for compatibility.
template <>
struct DBusSignature<const char*> {
  static constexpr auto kValue = internal::ToArray("s");
};

template <>
struct DBusSignature<::dbus::ObjectPath> {
  static constexpr auto kValue = internal::ToArray("o");
};

template <>
struct DBusSignature<brillo::Any> {
  static constexpr auto kValue = internal::ToArray("v");
};

template <>
struct DBusSignature<base::ScopedFD> {
  static constexpr auto kValue = internal::ToArray("h");
};

template <typename T, typename Alloc>
struct DBusSignature<std::vector<T, Alloc>> {
  static constexpr auto kValue =
      internal::StrJoin("a", DBusSignature<T>::kValue);
};

template <typename U, typename V>
struct DBusSignature<std::pair<U, V>> {
  static constexpr auto kValue = internal::StrJoin(
      "(", DBusSignature<U>::kValue, DBusSignature<V>::kValue, ")");
};

template <typename... Ts>
struct DBusSignature<std::tuple<Ts...>> {
  static constexpr auto kValue =
      internal::StrJoin("(", DBusSignature<Ts>::kValue..., ")");
};

template <typename K, typename V, typename Comp, typename Alloc>
struct DBusSignature<std::map<K, V, Comp, Alloc>> {
  static constexpr auto kValue = internal::StrJoin(
      "a{", DBusSignature<K>::kValue, DBusSignature<V>::kValue, "}");
};

// Support of protobuf.
template <typename T>
struct DBusSignature<T, std::enable_if_t<internal::IsProtobuf<T>::value>> {
  // Use byte array for underlying storage.
  static constexpr auto kValue = internal::ToArray("ay");
};

//----------------------------------------------------------------------------
// Get D-Bus data signature from C++ data types.
// Specializations of a generic GetDBusSignature<T>() provide signature strings
// for native C++ types. This function is available only for type supported
// by D-Bus.
template <typename T>
constexpr const char* GetDBusSignature() {
  return DBusSignature<T>::kValue.data();
}

}  // namespace brillo::dbus_utils

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_SIGNATURE_H_
