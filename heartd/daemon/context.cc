// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/context.h"

#include <memory>

namespace heartd {

Context::Context() {
  database_ = std::make_unique<Database>();
}

Context::~Context() = default;

}  // namespace heartd
