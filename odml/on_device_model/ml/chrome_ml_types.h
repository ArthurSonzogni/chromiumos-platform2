// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_H_
#define ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_H_

#include <string>
#include <variant>

namespace ml {

enum class Token {
  // Prefix for system text.
  kSystem,
  // Prefix for model text.
  kModel,
  // Prefix for user text.
  kUser,
  // End a system/model/user section.
  kEnd,
};

// TODO(b/353900545): Fix the skia code, and sync skia definition with g3.
struct SkBitmap {};

using InputPiece = std::variant<Token, std::string, SkBitmap, bool>;

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_H_
