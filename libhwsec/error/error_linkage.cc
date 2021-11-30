// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "libhwsec-foundation/status/impl/error.h"

namespace hwsec_foundation {
namespace status {
namespace __impl {

// See the comment in "libhwsec-foundation/status/impl/error.h" for why we need
// these external definitions.
Error::Error(std::string message) : message_(message) {}

}  // namespace __impl
}  // namespace status
}  // namespace hwsec_foundation
