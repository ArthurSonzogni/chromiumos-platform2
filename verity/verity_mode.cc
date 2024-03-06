// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "verity/verity_mode.h"

#include <base/containers/fixed_flat_map.h>

namespace verity {

namespace {
constexpr auto kVerityModeStrMap =
    base::MakeFixedFlatMap<std::string_view, verity_mode_t>({
        {kVerityModeCreate, VERITY_CREATE},
        {kVerityModeVerify, VERITY_VERIFY},
    });
}  // namespace

verity_mode_t ToVerityMode(const std::string& verity_mode_str) {
  auto itr = kVerityModeStrMap.find(verity_mode_str);
  return itr == kVerityModeStrMap.end() ? VERITY_NONE : itr->second;
}

}  // namespace verity
