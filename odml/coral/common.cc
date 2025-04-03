// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/common.h"

#include <string>

#include <base/containers/contains.h>
#include <base/containers/fixed_flat_set.h>

namespace coral {

bool IsLanguageSupported(const std::string& locale) {
  static constexpr auto kSupportedLanguages =
      base::MakeFixedFlatSet<std::string_view>({"en", "ja", "de", "fr", "da",
                                                "es", "it", "pt", "nl", "no",
                                                "fi", "sv"});
  return base::Contains(kSupportedLanguages, locale);
}

}  // namespace coral
