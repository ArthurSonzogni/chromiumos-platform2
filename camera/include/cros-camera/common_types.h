/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_COMMON_TYPES_H_
#define CAMERA_INCLUDE_CROS_CAMERA_COMMON_TYPES_H_

#include <cstdint>
#include <string>

#include <base/strings/stringprintf.h>

namespace cros {

/**
 * Rect follows rectangular coordinate system for images. (0, 0) is the
 * top-left corner. It can be used to present the coordinates of active sensory
 * array and bounding box of detected faces.
 */
template <typename T>
struct Rect {
  T left;
  T top;
  T right;
  T bottom;

  Rect() : left(0), top(0), right(0), bottom(0) {}
  Rect(T l, T t, T r, T b) : left(l), top(t), right(r), bottom(b) {}
  bool is_valid() const { return left < right && top < bottom; }
  T width() const { return right - left; }
  T height() const { return bottom - top; }
};

struct Size {
  Size(uint32_t w, uint32_t h) : width(w), height(h) {}
  uint32_t width;
  uint32_t height;
  uint32_t area() const { return width * height; }
  bool operator<(const Size& rhs) const {
    if (area() == rhs.area()) {
      return width < rhs.width;
    }
    return area() < rhs.area();
  }
  bool operator==(const Size& rhs) const {
    return width == rhs.width && height == rhs.height;
  }
  std::string ToString() const {
    return base::StringPrintf("%ux%u", width, height);
  }
};

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_COMMON_TYPES_H_
