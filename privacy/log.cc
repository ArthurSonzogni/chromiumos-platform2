// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privacy/log.h"

#include <base/logging.h>

namespace privacy {

std::ostream& operator<<(std::ostream& out, privacy::PrivacyMetadata metadata) {
  // TODO(b/338062698): sensitive metadata logged
  return out << '[' << metadata.piiType << "] " << metadata.value;
}
}  // namespace privacy
