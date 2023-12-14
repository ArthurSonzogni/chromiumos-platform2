// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/metadata.h"

#include <variant>

#include <base/functional/overloaded.h>

#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {

namespace {

template <typename T>
const T* OptionalToPtr(const std::optional<T>& opt) {
  if (opt.has_value()) {
    return &*opt;
  }
  return nullptr;
}

}  // namespace

const SerializedKnowledgeFactorHashInfo* AuthFactorMetadata::hash_info() const {
  return std::visit<const SerializedKnowledgeFactorHashInfo*>(
      base::Overloaded{
          [](const PasswordMetadata& pw) {
            return OptionalToPtr(pw.hash_info);
          },
          [](const PinMetadata& pin) { return OptionalToPtr(pin.hash_info); },
          [](const auto&) { return nullptr; },
      },
      metadata);
}

}  // namespace cryptohome
