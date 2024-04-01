/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/common_types.h"

#include <base/check.h>

namespace cros {

RelativeFov::RelativeFov(float x, float y) : x_(x), y_(y) {
  CHECK(IsValid());
}

RelativeFov::RelativeFov(const Size& image_size,
                         const Size& active_array_size) {
  const float iw_ah =
      static_cast<float>(image_size.width * active_array_size.height);
  const float ih_aw =
      static_cast<float>(image_size.height * active_array_size.width);
  if (iw_ah >= ih_aw) {
    *this = RelativeFov(1.0f, ih_aw / iw_ah);
  } else {
    *this = RelativeFov(iw_ah / ih_aw, 1.0f);
  }
}

bool RelativeFov::operator==(const RelativeFov& other) const {
  CHECK(IsValid());
  CHECK(other.IsValid());
  return std::abs(x_ - other.x_) <= kEpsilon &&
         std::abs(y_ - other.y_) <= kEpsilon;
}

bool RelativeFov::IsValid() const {
  return x_ > 0.0f && x_ <= 1.0f && y_ > 0.0f && y_ <= 1.0f;
}

bool RelativeFov::Covers(const RelativeFov& other) const {
  CHECK(IsValid());
  CHECK(other.IsValid());
  return x_ + kEpsilon >= other.x_ && y_ + kEpsilon >= other.y_;
}

Rect<float> RelativeFov::GetCropWindowInto(const RelativeFov& other) const {
  CHECK(IsValid());
  CHECK(other.IsValid());
  CHECK(Covers(other));
  const float w = std::min(other.x_ / x_, 1.0f);
  const float h = std::min(other.y_ / y_, 1.0f);
  return Rect<float>((1.0f - w) * 0.5f, (1.0f - h) * 0.5f, w, h);
}

}  // namespace cros
