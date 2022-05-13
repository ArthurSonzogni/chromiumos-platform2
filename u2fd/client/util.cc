// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/client/util.h"

#include <string>
#include <vector>

namespace u2f {
namespace clientutil {

template <>
void AppendToVector(const std::vector<uint8_t>& from,
                    std::vector<uint8_t>* to) {
  to->insert(to->end(), from.begin(), from.end());
}

template <>
void AppendToVector(const std::string& from, std::vector<uint8_t>* to) {
  to->insert(to->end(), from.begin(), from.end());
}

void AppendSubstringToVector(const std::string& from,
                             int start,
                             int length,
                             std::vector<uint8_t>* to) {
  to->insert(to->end(), from.begin() + start, from.begin() + start + length);
}

}  // namespace clientutil
}  // namespace u2f
