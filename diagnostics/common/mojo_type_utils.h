// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_MOJO_TYPE_UTILS_H_
#define DIAGNOSTICS_COMMON_MOJO_TYPE_UTILS_H_

#include <string>
#include <type_traits>

#include <mojo/public/cpp/bindings/struct_ptr.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Type traits templates for identifying the mojo StructPtr types.
template <typename>
class IsStructPtr : public std::false_type {};
template <typename T>
class IsStructPtr<::mojo::StructPtr<T>> : public std::true_type {};
template <typename T>
class IsStructPtr<::mojo::InlinedStructPtr<T>> : public std::true_type {};

// Type traits templates for identifying the mojo Union types.
template <typename T>
class HasWhichFunction {
  template <typename U>
  static int8_t test_funtion(decltype(&U::which));
  template <typename U>
  static int16_t test_funtion(...);

 public:
  static constexpr bool value = (sizeof(test_funtion<T>(0)) == sizeof(int8_t));
};
template <typename T>
class IsMojoUnion : public HasWhichFunction<T> {};

// Helper type for better compiler error message.
template <typename T>
class UndefinedMojoType : public std::false_type {};

// Returns the difference between |a| and |b|. This is for the debugging of the
// unittest related to mojo types. Each mojo type needs to be manually defined.
// Example usage:
//    EXPECT_EQ(a, b) << GetDiffString(a, b);
//
// Example output:
//    field_a:
//       [Equal]
//    field_b:
//       "a value" vs "another value"
template <typename T>
std::string GetDiffString(const T& a, const T& b) {
  if (a == b)
    return "[Equal]";
  if constexpr (IsStructPtr<T>::value) {
    if (a.is_null())
      return "[null] vs [non-null]";
    if (b.is_null())
      return "[non-null] vs [null]";
    return GetDiffString(*a, *b);
  } else if constexpr (std::is_enum_v<T>) {
    return GetDiffString("[Enum]:" + std::to_string(static_cast<int>(a)),
                         "[Enum]:" + std::to_string(static_cast<int>(b)));
  } else if constexpr (std::is_arithmetic_v<T>) {
    return GetDiffString(std::to_string(a), std::to_string(b));
  } else {
    static_assert(UndefinedMojoType<T>::value,
                  "Undefined type for GetDiffString().");
  }
}

template <>
std::string GetDiffString<std::string>(const std::string& a,
                                       const std::string& b);

template <>
std::string GetDiffString<base::Optional<std::string>>(
    const base::Optional<std::string>& a, const base::Optional<std::string>& b);

template <>
std::string GetDiffString<::chromeos::cros_healthd::mojom::NullableUint64>(
    const ::chromeos::cros_healthd::mojom::NullableUint64& a,
    const ::chromeos::cros_healthd::mojom::NullableUint64& b);

template <>
std::string GetDiffString<::chromeos::cros_healthd::mojom::VpdInfo>(
    const ::chromeos::cros_healthd::mojom::VpdInfo& a,
    const ::chromeos::cros_healthd::mojom::VpdInfo& b);

template <>
std::string GetDiffString<::chromeos::cros_healthd::mojom::DmiInfo>(
    const ::chromeos::cros_healthd::mojom::DmiInfo& a,
    const ::chromeos::cros_healthd::mojom::DmiInfo& b);

template <>
std::string GetDiffString<::chromeos::cros_healthd::mojom::OsVersion>(
    const ::chromeos::cros_healthd::mojom::OsVersion& a,
    const ::chromeos::cros_healthd::mojom::OsVersion& b);

template <>
std::string GetDiffString<::chromeos::cros_healthd::mojom::OsInfo>(
    const ::chromeos::cros_healthd::mojom::OsInfo& a,
    const ::chromeos::cros_healthd::mojom::OsInfo& b);

template <>
std::string GetDiffString<::chromeos::cros_healthd::mojom::SystemInfoV2>(
    const ::chromeos::cros_healthd::mojom::SystemInfoV2& a,
    const ::chromeos::cros_healthd::mojom::SystemInfoV2& b);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_MOJO_TYPE_UTILS_H_
