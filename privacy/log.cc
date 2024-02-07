// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privacy/log.h"

#include <string>
#include <vector>

#include <base/logging.h>

namespace privacy {
void PersistMarkers(std::vector<privacy::PIIType> piiTypeList) {
  std::string all_pii_strings;
  for (const logging::PIIType piiType : piiTypeList) {
    all_pii_strings += std::to_string(piiType) + ", ";
  }
  LOG(WARNING) << "The following log might contain PII data:"
               << all_pii_strings;
}
}  // namespace privacy
