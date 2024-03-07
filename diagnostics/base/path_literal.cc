// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/base/path_literal.h"

#include <cstddef>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/base/file_utils.h"

namespace diagnostics {

PathLiteral::PathLiteral(const char** tokens, std::size_t size) {
  for (int i = 0; i < size; ++i) {
    tokens_.push_back(tokens[i]);
  }
}

PathLiteral::~PathLiteral() = default;

base::FilePath PathLiteral::ToPath() const {
  base::FilePath path{tokens_[0]};
  for (int i = 1; i < tokens_.size(); ++i) {
    path = path.Append(tokens_[i]);
  }
  return path;
}

std::string PathLiteral::ToStr() const {
  return ToPath().value();
}

base::FilePath PathLiteral::ToFull() const {
  return GetRootDir().Append(ToPath());
}

}  // namespace diagnostics
