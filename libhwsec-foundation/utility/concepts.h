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

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_UTILITY_CONCEPTS_H_
