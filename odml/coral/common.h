// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_COMMON_H_
#define ODML_CORAL_COMMON_H_

#include <base/types/expected.h>

#include "odml/mojom/coral_service.mojom.h"

namespace coral {

template <class T>
using CoralResult = base::expected<T, mojom::CoralError>;

// Used as parent struct of Response types, which we want to enforce move-only.
struct MoveOnly {
  MoveOnly() = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
  bool operator==(const MoveOnly&) const = default;
};

}  // namespace coral

#endif  // ODML_CORAL_COMMON_H_
