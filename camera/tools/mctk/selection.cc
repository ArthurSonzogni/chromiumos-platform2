/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/selection.h"

#include <optional>

bool V4lMcSelection::HasAny() {
  return this->crop_.has_value() || this->crop_default_.has_value() ||
         this->crop_bounds_.has_value() || this->native_size_.has_value() ||
         this->compose_.has_value() || this->compose_default_.has_value() ||
         this->compose_bounds_.has_value() || this->compose_padded_.has_value();
}
