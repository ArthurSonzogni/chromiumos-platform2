// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/common.h"

#include <string>

#include <base/containers/fixed_flat_set.h>

namespace coral {

bool IsLanguageSupported(const std::string& locale) {
  static constexpr auto kSupportedLanguages =
      base::MakeFixedFlatSet<std::string_view>({"en", "ja", "de", "fr", "da",
                                                "es", "it", "pt", "nl", "no",
                                                "fi", "sv"});
  return kSupportedLanguages.contains(locale);
}

bool IsLanguageSupportedBySafetyModel(const std::string& locale) {
  static constexpr auto kSupportedLanguagesBySafetyModel =
      base::MakeFixedFlatSet<std::string_view>(
          {"en", "ja", "de", "fr", "da", "es", "it", "nl", "sv"});
  return kSupportedLanguagesBySafetyModel.contains(locale);
}

}  // namespace coral
