// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_CLIENT_UTIL_H_
#define U2FD_CLIENT_UTIL_H_

#include <algorithm>
#include <string>
#include <vector>

#include "u2fd/client/u2f_client_export.h"

namespace u2f {
namespace clientutil {

// Utility function to copy an object, as raw bytes, to a vector.
template <typename FromType>
void U2F_CLIENT_EXPORT AppendToVector(const FromType& from,
                                      std::vector<uint8_t>* to) {
  const uint8_t* from_bytes = reinterpret_cast<const uint8_t*>(&from);
  std::copy(from_bytes, from_bytes + sizeof(from), std::back_inserter(*to));
}

// Specializations of above function for copying from vector and string.
template <>
void U2F_CLIENT_EXPORT AppendToVector(const std::vector<uint8_t>& from,
                                      std::vector<uint8_t>* to);
template <>
void U2F_CLIENT_EXPORT AppendToVector(const std::string& from,
                                      std::vector<uint8_t>* to);

// Utility function to copy part of a string to a vector.
void AppendSubstringToVector(const std::string& from,
                             int start,
                             int length,
                             std::vector<uint8_t>* to);

}  // namespace clientutil
}  // namespace u2f

#endif  // U2FD_CLIENT_UTIL_H_
